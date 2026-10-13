#include <windows.h>
#include <string.h>
#include "worker.h"
#include "memory.h"
#include "process.h"
#include "syscalls.h"
#include "files.h"
#include "path.h"
#include "procfs.h"


LONG resolve_at(DWORD directory, DWORD pointer, char *absolute, int *empty) {
    char guest[JOLT_GUEST_PATH];
    LONG length = guest_string(pointer, guest, sizeof guest);
    if (length < 0) return length;
    if (empty) *empty = 0;
    if (!length) {
        if (!empty) return error(LINUX_ENOENT);
        *empty = 1;
        absolute[0] = 0;
        return 0;
    }
    const char *base = state.cwd;
    if (guest[0] != '/' && directory != LINUX_AT_FDCWD) {
        int index = fd_description(directory);
        if (index < 0) return error(LINUX_EBADF);
        if (description_at(index)->type != DESCRIPTION_DIRECTORY) return error(LINUX_ENOTDIR);
        base = description_at(index)->path;
    }
    LONG result = jolt_absolute_path(base, guest, absolute, JOLT_GUEST_PATH);
    if (result || guest[length - 1] != '/') return result;
    struct jolt_node node;
    if (jolt_lookup(absolute, &node)) return 0;
    return node.kind == NODE_DIRECTORY || node.kind == NODE_VIRTUAL_DIRECTORY ? 0 : error(LINUX_ENOTDIR);
}

static LONG resolve(DWORD directory, DWORD pointer, char *absolute) {
    return resolve_at(directory, pointer, absolute, NULL);
}

LONG sys_getdents64(const DWORD *arguments) {
    DWORD fd = arguments[0], buffer = arguments[1], capacity = arguments[2];
    int index = fd_description(fd);
    if (index < 0) return error(LINUX_EBADF);
    if (description_at(index)->type != DESCRIPTION_DIRECTORY) return error(LINUX_ENOTDIR);
    if (!guest_writable(buffer, capacity)) return error(LINUX_EFAULT);
    jolt_lock();
    LONG result = jolt_file_getdents(index, (void *)buffer, capacity);
    jolt_unlock();
    return result;
}

static LONG stat_descriptor(DWORD fd, struct linux_statx *record) {
    if (fd == LINUX_AT_FDCWD) return jolt_file_statx(-1, state.cwd, record);
    int index = fd_description(fd);
    if (index < 0) return error(LINUX_EBADF);
    jolt_lock();
    LONG result = jolt_file_statx(index, NULL, record);
    jolt_unlock();
    return result;
}

LONG sys_statx(const DWORD *arguments) {
    DWORD directory = arguments[0], path = arguments[1], flags = arguments[2], buffer = arguments[4];
    if (!guest_writable(buffer, sizeof(struct linux_statx))) return error(LINUX_EFAULT);
    char absolute[JOLT_GUEST_PATH];
    int empty = 0;
    LONG result = resolve_at(directory, path, absolute, (flags & LINUX_AT_EMPTY_PATH) ? &empty : NULL);
    if (result) return result;
    struct linux_statx record;
    result = empty ? stat_descriptor(directory, &record) : jolt_file_statx(-1, absolute, &record);
    if (!result) guest_copy_out(buffer, &record, sizeof record);
    return result;
}

static LONG unlink_at(DWORD directory, DWORD path, int remove_directory) {
    char absolute[JOLT_GUEST_PATH];
    LONG result = resolve(directory, path, absolute);
    return result ? result : jolt_file_unlink(absolute, remove_directory);
}

LONG sys_unlink(const DWORD *arguments) {
    return unlink_at(LINUX_AT_FDCWD, arguments[0], 0);
}

LONG sys_unlinkat(const DWORD *arguments) {
    return unlink_at(arguments[0], arguments[1], (arguments[2] & LINUX_AT_REMOVEDIR) != 0);
}

LONG sys_rmdir(const DWORD *arguments) {
    return unlink_at(LINUX_AT_FDCWD, arguments[0], 1);
}

static LONG make_directory_at(DWORD directory, DWORD path) {
    char absolute[JOLT_GUEST_PATH];
    LONG result = resolve(directory, path, absolute);
    return result ? result : jolt_file_mkdir(absolute);
}

LONG sys_mkdir(const DWORD *arguments) {
    return make_directory_at(LINUX_AT_FDCWD, arguments[0]);
}

LONG sys_mkdirat(const DWORD *arguments) {
    return make_directory_at(arguments[0], arguments[1]);
}

static LONG access_at(DWORD directory, DWORD path, DWORD mode) {
    char absolute[JOLT_GUEST_PATH];
    LONG result = resolve(directory, path, absolute);
    return result ? result : jolt_file_access(absolute, mode);
}

LONG sys_access(const DWORD *arguments) {
    return access_at(LINUX_AT_FDCWD, arguments[0], arguments[1]);
}

LONG sys_faccessat(const DWORD *arguments) {
    return access_at(arguments[0], arguments[1], arguments[2]);
}

static LONG change_mode_at(DWORD directory, DWORD path, DWORD mode) {
    char absolute[JOLT_GUEST_PATH];
    LONG result = resolve(directory, path, absolute);
    return result ? result : jolt_file_chmod(absolute, mode);
}

LONG sys_chmod(const DWORD *arguments) {
    return change_mode_at(LINUX_AT_FDCWD, arguments[0], arguments[1]);
}

LONG sys_fchmodat(const DWORD *arguments) {
    return change_mode_at(arguments[0], arguments[1], arguments[2]);
}

static LONG require_existing(DWORD directory, DWORD path) {
    char absolute[JOLT_GUEST_PATH];
    struct jolt_node node;
    LONG result = resolve(directory, path, absolute);
    return result ? result : jolt_lookup(absolute, &node);
}

LONG sys_chown(const DWORD *arguments) {
    return require_existing(LINUX_AT_FDCWD, arguments[0]);
}

LONG sys_fchownat(const DWORD *arguments) {
    return require_existing(arguments[0], arguments[1]);
}

static LONG truncate_path(DWORD path, DWORD length) {
    char absolute[JOLT_GUEST_PATH];
    LONG result = resolve(LINUX_AT_FDCWD, path, absolute);
    return result ? result : jolt_file_truncate(-1, absolute, length);
}

LONG sys_truncate(const DWORD *arguments) {
    return truncate_path(arguments[0], arguments[1]);
}

LONG sys_truncate64(const DWORD *arguments) {
    return truncate_path(arguments[0], arguments[2]);
}

static LONG rename_at(DWORD old_directory, DWORD old_path, DWORD new_directory, DWORD new_path) {
    char source[JOLT_GUEST_PATH], destination[JOLT_GUEST_PATH];
    LONG result = resolve(old_directory, old_path, source);
    if (!result) result = resolve(new_directory, new_path, destination);
    return result ? result : jolt_file_rename(source, destination);
}

LONG sys_rename(const DWORD *arguments) {
    return rename_at(LINUX_AT_FDCWD, arguments[0], LINUX_AT_FDCWD, arguments[1]);
}

LONG sys_renameat(const DWORD *arguments) {
    return rename_at(arguments[0], arguments[1], arguments[2], arguments[3]);
}

LONG sys_chdir(const DWORD *arguments) {
    char absolute[JOLT_GUEST_PATH];
    struct jolt_node node;
    LONG result = resolve(LINUX_AT_FDCWD, arguments[0], absolute);
    if (!result) result = jolt_lookup(absolute, &node);
    if (result) return result;
    if (node.kind != NODE_DIRECTORY && node.kind != NODE_VIRTUAL_DIRECTORY) return error(LINUX_ENOTDIR);
    strcpy(state.cwd, absolute);
    return 0;
}

LONG sys_fchdir(const DWORD *arguments) {
    int index = fd_description(arguments[0]);
    if (index < 0) return error(LINUX_EBADF);
    if (description_at(index)->type != DESCRIPTION_DIRECTORY) return error(LINUX_ENOTDIR);
    strcpy(state.cwd, description_at(index)->path);
    return 0;
}

LONG sys_getcwd(const DWORD *arguments) {
    DWORD buffer = arguments[0], capacity = arguments[1];
    DWORD length = (DWORD)strlen(state.cwd) + 1;
    if (capacity < length) return error(LINUX_ERANGE);
    return guest_copy_out(buffer, state.cwd, length) ? (LONG)length : error(LINUX_EFAULT);
}

static LONG read_link_at(DWORD directory, DWORD path, DWORD buffer, DWORD capacity) {
    char absolute[JOLT_GUEST_PATH];
    struct jolt_node node;
    LONG result = resolve(directory, path, absolute);
    if (result) return result;
    if (procfs_owns(absolute)) {
        char target[JOLT_GUEST_PATH];
        LONG length = procfs_readlink(absolute, target, capacity < sizeof target ? capacity : sizeof target);
        if (length < 0) return length;
        return guest_copy_out(buffer, target, (DWORD)length) ? length : error(LINUX_EFAULT);
    }
    result = jolt_lookup(absolute, &node);
    return result ? result : error(LINUX_EINVAL);
}

LONG sys_readlink(const DWORD *arguments) {
    return read_link_at(LINUX_AT_FDCWD, arguments[0], arguments[1], arguments[2]);
}

LONG sys_readlinkat(const DWORD *arguments) {
    return read_link_at(arguments[0], arguments[1], arguments[2], arguments[3]);
}

LONG sys_utimensat(const DWORD *arguments) {
    DWORD directory = arguments[0], path = arguments[1];
    if (!path) {
        int index = fd_description(directory);
        if (index < 0) return error(LINUX_EBADF);
        if (description_at(index)->type != DESCRIPTION_FILE) return 0;
        return jolt_file_touch(description_at(index)->path);
    }
    char absolute[JOLT_GUEST_PATH];
    LONG result = resolve(directory, path, absolute);
    return result ? result : jolt_file_touch(absolute);
}

static LONG copy_statfs(DWORD size, DWORD buffer) {
    struct linux_statfs64 record;
    if (size != sizeof record && size != LINUX_STATFS64_PACKED_SIZE) return error(LINUX_EINVAL);
    LONG result = jolt_file_statfs(&record);
    if (result) return result;
    return guest_copy_out(buffer, &record, size) ? 0 : error(LINUX_EFAULT);
}

LONG sys_statfs64(const DWORD *arguments) {
    char absolute[JOLT_GUEST_PATH];
    struct jolt_node node;
    LONG result = resolve(LINUX_AT_FDCWD, arguments[0], absolute);
    if (!result) result = jolt_lookup(absolute, &node);
    return result ? result : copy_statfs(arguments[1], arguments[2]);
}

LONG sys_fstatfs64(const DWORD *arguments) {
    if (fd_description(arguments[0]) < 0) return error(LINUX_EBADF);
    return copy_statfs(arguments[1], arguments[2]);
}

LONG sys_umask(const DWORD *arguments) {
    DWORD previous = state.umask;
    state.umask = arguments[0] & 0777;
    return (LONG)previous;
}
