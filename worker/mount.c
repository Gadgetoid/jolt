#include <windows.h>
#include <string.h>
#include "text.h"
#include "worker.h"
#include "memory.h"
#include "syscalls.h"
#include "files.h"
#include "process.h"

#define FILESYSTEM_TYPE "cefs"
#define TYPE_CAPACITY 16
#define LINUX_MS_REMOUNT 32
#define LINUX_MS_BIND 4096
#define LINUX_MS_MOVE 8192

static LONG normalise_source(const char *raw, char *source) {
    size_t used = 0;
    for (const char *cursor = raw; *cursor; cursor++) {
        unsigned char character = (unsigned char)*cursor;
        if (character >= 128) return error(LINUX_EILSEQ);
        if (character < 32 || strchr(":*?\"<>|", character)) return error(LINUX_EINVAL);
        char next = character == '/' ? '\\' : (char)character;
        if (next == '\\' && used && source[used - 1] == '\\') continue;
        if (!used && next != '\\') source[used++] = '\\';
        if (used >= JOLT_MOUNT_SOURCE - 1) return error(LINUX_ENAMETOOLONG);
        source[used++] = next;
    }
    while (used && source[used - 1] == '\\') used--;
    source[used] = 0;
    return 0;
}

static LONG check_source(const char *source) {
    WCHAR path[MAX_PATH];
    if (!source[0]) return 0;
    jolt_wide_from_ascii(source, path, MAX_PATH);
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_ATTRIBUTES) return error(LINUX_ENOENT);
    return attributes & FILE_ATTRIBUTE_DIRECTORY ? 0 : error(LINUX_ENOTDIR);
}

static int find_target(const char *target) {
    for (int index = 0; index < JOLT_MOUNTS; index++)
        if (!strcmp(kernel()->mounts[index].target, target)) return index;
    return -1;
}

LONG sys_mount(const DWORD *arguments) {
    char raw[JOLT_MOUNT_SOURCE], source[JOLT_MOUNT_SOURCE], type[TYPE_CAPACITY], target[JOLT_GUEST_PATH];
    DWORD flags = arguments[3];
    if (flags & (LINUX_MS_REMOUNT | LINUX_MS_BIND | LINUX_MS_MOVE)) return error(LINUX_EINVAL);
    LONG result = guest_string(arguments[2], type, sizeof type);
    if (result < 0) return result;
    if (strcmp(type, FILESYSTEM_TYPE)) return error(LINUX_ENODEV);
    if ((result = guest_string(arguments[0], raw, sizeof raw)) < 0) return result;
    if ((result = normalise_source(raw, source)) || (result = check_source(source))) return result;
    if ((result = resolve_at(LINUX_AT_FDCWD, arguments[1], target, NULL))) return result;
    struct jolt_node node;
    if ((result = jolt_lookup(target, &node))) return result;
    if (node.kind != NODE_DIRECTORY || !target[1]) return error(LINUX_ENOTDIR);
    jolt_lock();
    int slot = find_target(target) >= 0 ? -2 : find_target("");
    if (slot >= 0) {
        strcpy(kernel()->mounts[slot].source, source);
        strcpy(kernel()->mounts[slot].target, target);
    }
    jolt_unlock();
    if (slot == -2) return error(LINUX_EBUSY);
    return slot < 0 ? error(LINUX_ENOSPC) : 0;
}

LONG sys_umount2(const DWORD *arguments) {
    char target[JOLT_GUEST_PATH];
    LONG result = resolve_at(LINUX_AT_FDCWD, arguments[0], target, NULL);
    if (result) return result;
    jolt_lock();
    int slot = find_target(target);
    if (slot >= 0) memset(&kernel()->mounts[slot], 0, sizeof kernel()->mounts[slot]);
    jolt_unlock();
    return slot < 0 ? error(LINUX_EINVAL) : 0;
}
