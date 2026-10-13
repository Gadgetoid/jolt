#include <string.h>
#include <wchar.h>
#include "files.h"
#include "linux.h"
#include "guest-manifest.h"
#include "procfs.h"
#include "hostpath.h"
#include "clipboard.h"
#include "serial.h"

#define UNIX_EPOCH_FILETIME 116444736000000000ULL
#define FILETIME_TICKS_PER_SECOND 10000000ULL
#define NANOSECONDS_PER_FILETIME_TICK 100
#define TRUNCATE_LIMIT 16777216UL
#define FILESYSTEM_MAJOR 8
#define FILESYSTEM_MINOR 1
#define MEMORY_DEVICE_MAJOR 1
#define NULL_DEVICE_MINOR 3
#define ZERO_DEVICE_MINOR 5
#define RANDOM_DEVICE_MINOR 9
#define TTY_DEVICE_MAJOR 5
#define MISC_DEVICE_MAJOR 10
#define CLIPBOARD_DEVICE_MINOR 240
#define SERIAL_DEVICE_MAJOR 4
#define SERIAL_DEVICE_MINOR_BASE 64
#define PIPE_INODE_BASE 0x70000000UL
#define FNV_OFFSET 2166136261UL
#define FNV_PRIME 16777619UL
#define BLOCK_SIZE 4096
#define SECTOR_SIZE 512
#define BACKUP_SUFFIX L".jolt-old"
#define FIRST_HOST_ENTRY 2
#define LISTING_ENDED 0xffffffffUL
#define INVALID_ATTRIBUTES 0xffffffffUL
#define ACCESS_EXECUTE 1
#define ACCESS_WRITE 2
#define ACCESS_MASK 7

struct host_file {
    DWORD generation;
    HANDLE handle;
    HANDLE search;
    WIN32_FIND_DATAW found;
    DWORD position;
    int listing;
};

struct virtual_directory {
    const char *path;
    const char *const *names;
    BYTE entry_type;
};

static const char *const device_names[] = { "clipboard", "console", "null", "random", "tty", "ttyS0", "ttyS1", "ttyS2", "ttyS3", "urandom", "zero", NULL };
static const char *const no_names[] = { NULL };

static const struct virtual_directory virtual_directories[] = {
    { "/bin", no_names, LINUX_DT_REG },
    { "/dev", device_names, LINUX_DT_CHR },
};

static WCHAR root[MAX_PATH];
static struct host_file host_files[JOLT_DESCRIPTIONS];

static LONG fail(enum linux_errno code) {
    return -(LONG)code;
}

static LONG file_error(void) {
    switch (GetLastError()) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_INVALID_NAME: return fail(LINUX_ENOENT);
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: return fail(LINUX_EACCES);
    case ERROR_FILE_EXISTS: case ERROR_ALREADY_EXISTS: return fail(LINUX_EEXIST);
    case ERROR_DIR_NOT_EMPTY: return fail(LINUX_ENOTEMPTY);
    case ERROR_DISK_FULL: return fail(LINUX_ENOSPC);
    case ERROR_NOT_SAME_DEVICE: return fail(LINUX_EXDEV);
    case ERROR_INVALID_PARAMETER: return fail(LINUX_EINVAL);
    default: return fail(LINUX_EIO);
    }
}

static int has_prefix(const char *text, const char *prefix) {
    return !strncmp(text, prefix, strlen(prefix));
}

int jolt_is_applet(const char *name) {
    for (const char *applet = jolt_applet_names; *applet; applet += strlen(applet) + 1)
        if (!strcmp(applet, name)) return 1;
    return 0;
}

static LONG host_path(const char *absolute, WCHAR *path) {
    return jolt_host_path(root, absolute, path);
}

static const struct virtual_directory *find_virtual_directory(const char *absolute) {
    for (size_t index = 0; index < sizeof virtual_directories / sizeof virtual_directories[0]; index++)
        if (!strcmp(absolute, virtual_directories[index].path)) return &virtual_directories[index];
    return NULL;
}

static int device_kind(const char *name) {
    if (!strcmp(name, "null")) return NODE_NULL;
    if (!strcmp(name, "zero")) return NODE_ZERO;
    if (!strcmp(name, "tty") || !strcmp(name, "console")) return NODE_TTY;
    if (!strcmp(name, "random") || !strcmp(name, "urandom")) return NODE_RANDOM;
    if (!strcmp(name, "clipboard")) return NODE_CLIPBOARD;
    if (serial_port_named(name) >= 0) return NODE_SERIAL;
    return NODE_MISSING;
}

static int proc_kind(const char *absolute) {
    switch (procfs_lookup(absolute)) {
    case PROCFS_DIRECTORY: case PROCFS_LINK: return NODE_VIRTUAL_DIRECTORY;
    case PROCFS_FILE: return NODE_PROC_FILE;
    case PROCFS_EXECUTABLE: return NODE_APPLET;
    default: return NODE_MISSING;
    }
}

static int virtual_node(const char *absolute, struct jolt_node *node) {
    memset(node, 0, sizeof *node);
    if (procfs_owns(absolute)) {
        node->kind = proc_kind(absolute);
        if (node->kind == NODE_APPLET) node->size = JOLT_GUEST_SIZE;
    } else if (find_virtual_directory(absolute)) {
        node->kind = NODE_VIRTUAL_DIRECTORY;
    } else if (has_prefix(absolute, "/bin/")) {
        node->kind = jolt_is_applet(absolute + strlen("/bin/")) ? NODE_APPLET : NODE_MISSING;
        node->size = JOLT_GUEST_SIZE;
    } else if (has_prefix(absolute, "/dev/")) {
        node->kind = device_kind(absolute + strlen("/dev/"));
    } else {
        return 0;
    }
    return 1;
}

static int writable_path(const char *absolute) {
    return absolute[1] && !has_prefix(absolute, "/bin") && !has_prefix(absolute, "/dev") && !has_prefix(absolute, "/proc");
}

static int executable_path(const char *absolute) {
    size_t length = strlen(absolute);
    return strstr(absolute, "/bin/") != NULL || (length > 3 && !strcmp(absolute + length - 3, ".sh"));
}

int jolt_files_init(const WCHAR *folder) {
    if (wcslen(folder) + 4 >= MAX_PATH) return 0;
    wcscpy(root, folder);
    wcscat(root, L"root");
    DWORD attributes = GetFileAttributesW(root);
    return attributes != INVALID_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

void jolt_files_drop(int index) {
    struct host_file *host = &host_files[index];
    if (host->handle) CloseHandle(host->handle);
    if (host->search && host->search != INVALID_HANDLE_VALUE) FindClose(host->search);
    memset(host, 0, sizeof *host);
}

void jolt_files_forget(void) {
    for (int index = 0; index < JOLT_DESCRIPTIONS; index++) jolt_files_drop(index);
}

LONG jolt_lookup(const char *absolute, struct jolt_node *node) {
    if (virtual_node(absolute, node)) return node->kind == NODE_MISSING ? fail(LINUX_ENOENT) : 0;
    WCHAR path[MAX_PATH];
    LONG result = host_path(absolute, path);
    if (result) return result;
    if (!absolute[1] || jolt_is_mount_point(absolute)) {
        node->kind = NODE_DIRECTORY;
        node->attributes = FILE_ATTRIBUTE_DIRECTORY;
        return 0;
    }
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(path, &data);
    if (search == INVALID_HANDLE_VALUE) {
        node->kind = NODE_MISSING;
        return fail(LINUX_ENOENT);
    }
    FindClose(search);
    node->kind = data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? NODE_DIRECTORY : NODE_FILE;
    node->attributes = data.dwFileAttributes;
    node->size = data.nFileSizeLow;
    node->modified = data.ftLastWriteTime;
    return 0;
}

static DWORD host_access(DWORD flags) {
    DWORD access = flags & LINUX_O_ACCMODE;
    if (access == LINUX_O_RDONLY) return GENERIC_READ;
    if (access == LINUX_O_WRONLY) return GENERIC_WRITE;
    return GENERIC_READ | GENERIC_WRITE;
}

static struct host_file *current_host_file(int index) {
    struct host_file *host = &host_files[index];
    DWORD generation = jolt.kernel->descriptions[index].generation;
    if (host->generation != generation) {
        jolt_files_drop(index);
        host->generation = generation;
    }
    return host;
}

static HANDLE open_handle(int index) {
    const struct jolt_description *description = &jolt.kernel->descriptions[index];
    struct host_file *host = current_host_file(index);
    if (host->handle) return host->handle;
    WCHAR path[MAX_PATH];
    if (host_path(description->path, path)) return NULL;
    HANDLE handle = CreateFileW(path, host_access(description->flags), FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) return NULL;
    host->handle = handle;
    return handle;
}

static LONG probe_host_file(const WCHAR *path, DWORD access, DWORD disposition) {
    HANDLE handle = CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, disposition,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) return file_error();
    CloseHandle(handle);
    return 0;
}

static LONG open_regular(const char *absolute, const struct jolt_node *node, DWORD flags) {
    DWORD access = flags & LINUX_O_ACCMODE;
    int missing = node->kind == NODE_MISSING;
    if (missing && !(flags & LINUX_O_CREAT)) return fail(LINUX_ENOENT);
    if (!missing && (flags & LINUX_O_CREAT) && (flags & LINUX_O_EXCL)) return fail(LINUX_EEXIST);
    if (!writable_path(absolute)) return fail(LINUX_EACCES);
    WCHAR path[MAX_PATH];
    LONG result = host_path(absolute, path);
    if (result) return result;
    if (missing) return probe_host_file(path, GENERIC_WRITE, CREATE_NEW);
    if (flags & LINUX_O_TRUNC) {
        if (access == LINUX_O_RDONLY) return fail(LINUX_EINVAL);
        return probe_host_file(path, GENERIC_WRITE, TRUNCATE_EXISTING);
    }
    DWORD probe = access == LINUX_O_RDONLY ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE;
    return probe_host_file(path, probe, OPEN_EXISTING);
}

LONG jolt_file_open(char *absolute, DWORD flags, DWORD *type) {
    struct jolt_node node;
    LONG result = jolt_lookup(absolute, &node);
    if (result && result != fail(LINUX_ENOENT)) return result;
    DWORD access = flags & LINUX_O_ACCMODE;
    if (access == LINUX_O_ACCMODE) return fail(LINUX_EINVAL);
    switch (node.kind) {
    case NODE_NULL:
        *type = DESCRIPTION_NULL;
        return 0;
    case NODE_ZERO:
        *type = DESCRIPTION_ZERO;
        return 0;
    case NODE_TTY:
        *type = DESCRIPTION_TTY;
        return 0;
    case NODE_RANDOM:
        *type = DESCRIPTION_RANDOM;
        return 0;
    case NODE_CLIPBOARD:
        *type = DESCRIPTION_FILE;
        return clipboard_open(absolute, flags);
    case NODE_SERIAL:
        *type = DESCRIPTION_SERIAL;
        return serial_open(serial_port_of(absolute));
    case NODE_PROC_FILE:
        if (access != LINUX_O_RDONLY) return fail(LINUX_EACCES);
        *type = DESCRIPTION_PROC;
        return 0;
    case NODE_APPLET:
        return access ? fail(LINUX_ETXTBSY) : fail(LINUX_EACCES);
    case NODE_DIRECTORY:
    case NODE_VIRTUAL_DIRECTORY:
        if (access || (flags & (LINUX_O_CREAT | LINUX_O_TRUNC))) return fail(LINUX_EISDIR);
        *type = DESCRIPTION_DIRECTORY;
        return 0;
    default:
        if (flags & LINUX_O_DIRECTORY) return node.kind == NODE_MISSING ? fail(LINUX_ENOENT) : fail(LINUX_ENOTDIR);
        *type = DESCRIPTION_FILE;
        return open_regular(absolute, &node, flags);
    }
}

LONG jolt_file_read(int index, void *buffer, DWORD length, LONG position) {
    struct jolt_description *description = &jolt.kernel->descriptions[index];
    HANDLE handle = open_handle(index);
    if (!handle) return file_error();
    DWORD offset = position >= 0 ? (DWORD)position : description->offset;
    if (SetFilePointer(handle, (LONG)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return file_error();
    DWORD count = 0;
    if (!ReadFile(handle, buffer, length, &count, NULL)) return file_error();
    if (position < 0) description->offset = offset + count;
    return (LONG)count;
}

LONG jolt_file_write(int index, const void *buffer, DWORD length, LONG position) {
    struct jolt_description *description = &jolt.kernel->descriptions[index];
    HANDLE handle = open_handle(index);
    if (!handle) return file_error();
    int appending = (description->flags & LINUX_O_APPEND) && position < 0;
    DWORD offset = position >= 0 ? (DWORD)position : description->offset;
    DWORD placed = appending ? SetFilePointer(handle, 0, NULL, FILE_END) : SetFilePointer(handle, (LONG)offset, NULL, FILE_BEGIN);
    if (placed == INVALID_SET_FILE_POINTER) return file_error();
    DWORD count = 0;
    if (!WriteFile(handle, buffer, length, &count, NULL)) return file_error();
    if (position < 0) description->offset = placed + count;
    return (LONG)count;
}

LONG jolt_file_seek(int index, LONG offset, DWORD origin) {
    struct jolt_description *description = &jolt.kernel->descriptions[index];
    if (description->type == DESCRIPTION_DIRECTORY) {
        if (offset || origin != FILE_BEGIN) return fail(LINUX_EINVAL);
        description->offset = 0;
        return 0;
    }
    LONG base;
    if (origin == FILE_BEGIN) {
        base = 0;
    } else if (origin == FILE_CURRENT) {
        base = (LONG)description->offset;
    } else if (origin == FILE_END) {
        HANDLE handle = open_handle(index);
        DWORD size = handle ? GetFileSize(handle, NULL) : INVALID_FILE_SIZE;
        if (size == INVALID_FILE_SIZE) return file_error();
        base = (LONG)size;
    } else {
        return fail(LINUX_EINVAL);
    }
    if ((offset > 0 && base > 0x7fffffffL - offset) || base + offset < 0) return fail(LINUX_EINVAL);
    description->offset = (DWORD)(base + offset);
    return (LONG)description->offset;
}

LONG jolt_file_sync(int index) {
    HANDLE handle = open_handle(index);
    if (!handle) return file_error();
    return FlushFileBuffers(handle) ? 0 : file_error();
}

LONG jolt_file_truncate(int index, const char *absolute, DWORD length) {
    if (length > TRUNCATE_LIMIT) return fail(LINUX_EFBIG);
    HANDLE handle;
    int owned = index < 0;
    if (owned) {
        WCHAR path[MAX_PATH];
        LONG result = host_path(absolute, path);
        if (result) return result;
        handle = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle == INVALID_HANDLE_VALUE) return file_error();
    } else {
        handle = open_handle(index);
        if (!handle) return file_error();
    }
    int truncated = SetFilePointer(handle, (LONG)length, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER && SetEndOfFile(handle);
    LONG result = truncated ? 0 : file_error();
    if (owned) CloseHandle(handle);
    return result;
}

static int put_entry(BYTE *buffer, DWORD capacity, DWORD *used, DWORD position, const char *name, BYTE type) {
    DWORD name_length = (DWORD)strlen(name) + 1;
    DWORD size = (offsetof(struct linux_dirent64, name) + name_length + 7) & ~7UL;
    if (size > capacity - *used) return 0;
    BYTE record[sizeof(struct linux_dirent64) + MAX_PATH + 8];
    struct linux_dirent64 *entry = (struct linux_dirent64 *)record;
    memset(record, 0, size);
    entry->inode = position + 2;
    entry->offset = position;
    entry->record_length = (WORD)size;
    entry->type = type;
    memcpy(entry->name, name, name_length);
    memcpy(buffer + *used, record, size);
    *used += size;
    return 1;
}

static LONG list_dots(struct jolt_description *description, BYTE *buffer, DWORD capacity, DWORD *used) {
    while (description->offset < FIRST_HOST_ENTRY) {
        const char *name = description->offset ? ".." : ".";
        if (!put_entry(buffer, capacity, used, description->offset + 1, name, LINUX_DT_DIR))
            return *used ? (LONG)*used : fail(LINUX_EINVAL);
        description->offset++;
    }
    return 0;
}

static LONG list_virtual(struct jolt_description *description, const struct virtual_directory *directory,
    BYTE *buffer, DWORD capacity, DWORD *used) {
    int applets = !strcmp(directory->path, "/bin");
    const char *name = applets ? jolt_applet_names : directory->names[0];
    DWORD item = 0;
    for (DWORD position = FIRST_HOST_ENTRY; name && *name; position++) {
        if (position >= description->offset) {
            if (!put_entry(buffer, capacity, used, position + 1, name, directory->entry_type))
                return *used ? (LONG)*used : fail(LINUX_EINVAL);
            description->offset = position + 1;
        }
        name = applets ? name + strlen(name) + 1 : directory->names[++item];
    }
    return (LONG)*used;
}

static void end_listing(struct host_file *host) {
    if (host->search && host->search != INVALID_HANDLE_VALUE) FindClose(host->search);
    host->search = INVALID_HANDLE_VALUE;
}

static LONG start_listing(struct host_file *host, const struct jolt_description *description) {
    end_listing(host);
    WCHAR path[MAX_PATH];
    LONG result = host_path(description->path, path);
    if (result) return result;
    size_t length = wcslen(path);
    if (length + 3 >= MAX_PATH) return fail(LINUX_ENAMETOOLONG);
    wcscat(path, length && path[length - 1] == L'\\' ? L"*" : L"\\*");
    host->search = FindFirstFileW(path, &host->found);
    host->listing = 1;
    host->position = host->search == INVALID_HANDLE_VALUE ? LISTING_ENDED : FIRST_HOST_ENTRY;
    while (host->position != LISTING_ENDED && host->position < description->offset) {
        if (FindNextFileW(host->search, &host->found)) host->position++;
        else host->position = LISTING_ENDED;
    }
    if (host->position == LISTING_ENDED) {
        host->position = description->offset;
        end_listing(host);
    }
    return 0;
}

static int ascii_name(const WCHAR *wide, char *name) {
    size_t length = 0;
    int valid = 1;
    for (; length < MAX_PATH - 1 && wide[length]; length++) {
        if (wide[length] >= 128) valid = 0;
        name[length] = (char)wide[length];
    }
    name[length] = 0;
    return valid;
}

static LONG list_host(struct jolt_description *description, struct host_file *host,
    BYTE *buffer, DWORD capacity, DWORD *used) {
    if (!host->listing || host->position != description->offset) {
        LONG result = start_listing(host, description);
        if (result) return result;
    }
    while (host->search != INVALID_HANDLE_VALUE) {
        char name[MAX_PATH];
        BYTE type = host->found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? LINUX_DT_DIR : LINUX_DT_REG;
        if (ascii_name(host->found.cFileName, name) && !put_entry(buffer, capacity, used, host->position + 1, name, type))
            return *used ? (LONG)*used : fail(LINUX_EINVAL);
        host->position++;
        description->offset = host->position;
        if (!FindNextFileW(host->search, &host->found)) end_listing(host);
    }
    return (LONG)*used;
}

static LONG list_proc(struct jolt_description *description, BYTE *buffer, DWORD capacity, DWORD *used) {
    char name[MAX_PATH];
    BYTE type;
    for (;;) {
        if (!procfs_entry(description->path, description->offset - FIRST_HOST_ENTRY, name, &type)) break;
        if (!put_entry(buffer, capacity, used, description->offset + 1, name, type))
            return *used ? (LONG)*used : fail(LINUX_EINVAL);
        description->offset++;
    }
    return (LONG)*used;
}

LONG jolt_file_getdents(int index, void *buffer, DWORD capacity) {
    struct jolt_description *description = &jolt.kernel->descriptions[index];
    struct host_file *host = current_host_file(index);
    DWORD used = 0;
    LONG result = list_dots(description, buffer, capacity, &used);
    if (result) return result;
    if (procfs_owns(description->path)) return list_proc(description, buffer, capacity, &used);
    const struct virtual_directory *directory = find_virtual_directory(description->path);
    if (directory) return list_virtual(description, directory, buffer, capacity, &used);
    return list_host(description, host, buffer, capacity, &used);
}

static void unix_timestamp(FILETIME time, struct linux_statx_timestamp *timestamp) {
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    if (value.QuadPart < UNIX_EPOCH_FILETIME) return;
    ULONGLONG since = value.QuadPart - UNIX_EPOCH_FILETIME;
    timestamp->seconds = (LONGLONG)(since / FILETIME_TICKS_PER_SECOND);
    timestamp->nanoseconds = (DWORD)(since % FILETIME_TICKS_PER_SECOND) * NANOSECONDS_PER_FILETIME_TICK;
}

static DWORD path_inode(const char *absolute) {
    DWORD hash = FNV_OFFSET;
    for (; *absolute; absolute++) {
        unsigned char character = (unsigned char)*absolute;
        if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
        hash = (hash ^ character) * FNV_PRIME;
    }
    return hash ? hash : 1;
}

struct file_identity {
    DWORD mode, major, minor, inode;
};

static int description_identity(const struct jolt_description *description, struct file_identity *identity) {
    switch (description->type) {
    case DESCRIPTION_TTY:
        *identity = (struct file_identity){ LINUX_S_IFCHR | 0620, TTY_DEVICE_MAJOR, 0, 0 };
        return 1;
    case DESCRIPTION_NULL:
        *identity = (struct file_identity){ LINUX_S_IFCHR | 0666, MEMORY_DEVICE_MAJOR, NULL_DEVICE_MINOR, 0 };
        return 1;
    case DESCRIPTION_ZERO:
        *identity = (struct file_identity){ LINUX_S_IFCHR | 0666, MEMORY_DEVICE_MAJOR, ZERO_DEVICE_MINOR, 0 };
        return 1;
    case DESCRIPTION_RANDOM:
        *identity = (struct file_identity){ LINUX_S_IFCHR | 0666, MEMORY_DEVICE_MAJOR, RANDOM_DEVICE_MINOR, 0 };
        return 1;
    case DESCRIPTION_SERIAL:
        *identity = (struct file_identity){ LINUX_S_IFCHR | 0660, SERIAL_DEVICE_MAJOR, SERIAL_DEVICE_MINOR_BASE + description->pipe, 0 };
        return 1;
    case DESCRIPTION_PIPE_READ:
    case DESCRIPTION_PIPE_WRITE:
        *identity = (struct file_identity){ LINUX_S_IFIFO | 0600, 0, 0, PIPE_INODE_BASE + description->pipe };
        return 1;
    case DESCRIPTION_STREAM:
        *identity = (struct file_identity){ LINUX_S_IFSOCK | 0777, 0, 0, PIPE_INODE_BASE + description->pipe };
        return 1;
    default:
        return 0;
    }
}

static void node_identity(const struct jolt_node *node, const char *absolute, struct file_identity *identity) {
    memset(identity, 0, sizeof *identity);
    switch (node->kind) {
    case NODE_FILE:
        identity->mode = LINUX_S_IFREG | (node->attributes & FILE_ATTRIBUTE_READONLY ? 0444 : 0644);
        if (executable_path(absolute)) identity->mode |= 0111;
        break;
    case NODE_DIRECTORY:
        identity->mode = LINUX_S_IFDIR | 0755;
        break;
    case NODE_VIRTUAL_DIRECTORY:
        identity->mode = LINUX_S_IFDIR | 0555;
        break;
    case NODE_APPLET:
        identity->mode = LINUX_S_IFREG | 0755;
        break;
    case NODE_PROC_FILE:
        identity->mode = LINUX_S_IFREG | 0444;
        break;
    case NODE_RANDOM:
        *identity = (struct file_identity){ LINUX_S_IFCHR | 0666, MEMORY_DEVICE_MAJOR, RANDOM_DEVICE_MINOR, 0 };
        break;
    case NODE_NULL:
        *identity = (struct file_identity){ LINUX_S_IFCHR | 0666, MEMORY_DEVICE_MAJOR, NULL_DEVICE_MINOR, 0 };
        break;
    case NODE_ZERO:
        *identity = (struct file_identity){ LINUX_S_IFCHR | 0666, MEMORY_DEVICE_MAJOR, ZERO_DEVICE_MINOR, 0 };
        break;
    case NODE_CLIPBOARD:
        *identity = (struct file_identity){ LINUX_S_IFCHR | 0666, MISC_DEVICE_MAJOR, CLIPBOARD_DEVICE_MINOR, 0 };
        break;
    case NODE_SERIAL:
        *identity = (struct file_identity){ LINUX_S_IFCHR | 0660, SERIAL_DEVICE_MAJOR,
            SERIAL_DEVICE_MINOR_BASE + (DWORD)serial_port_of(absolute), 0 };
        break;
    default:
        *identity = (struct file_identity){ LINUX_S_IFCHR | 0620, TTY_DEVICE_MAJOR, 0, 0 };
        break;
    }
}

static void fill_statx(struct linux_statx *record, const struct file_identity *identity, const struct jolt_node *node) {
    memset(record, 0, sizeof *record);
    record->mask = LINUX_STATX_BASIC_STATS;
    record->block_size = BLOCK_SIZE;
    record->links = (identity->mode & LINUX_S_IFMT) == LINUX_S_IFDIR ? 2 : 1;
    record->mode = (WORD)identity->mode;
    record->inode = identity->inode;
    record->size = node->size;
    record->blocks = (node->size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    unix_timestamp(node->modified, &record->accessed);
    unix_timestamp(node->modified, &record->changed);
    unix_timestamp(node->modified, &record->modified);
    record->device_major = identity->major;
    record->device_minor = identity->minor;
    record->filesystem_major = FILESYSTEM_MAJOR;
    record->filesystem_minor = FILESYSTEM_MINOR;
}

LONG jolt_file_statx(int index, const char *absolute, struct linux_statx *record) {
    struct jolt_node node;
    struct file_identity identity;
    memset(&node, 0, sizeof node);
    if (index >= 0) {
        const struct jolt_description *description = &jolt.kernel->descriptions[index];
        if (description_identity(description, &identity)) {
            fill_statx(record, &identity, &node);
            return 0;
        }
        absolute = description->path;
    }
    LONG result = jolt_lookup(absolute, &node);
    if (result) return result;
    if (index >= 0 && node.kind == NODE_FILE) {
        HANDLE handle = open_handle(index);
        DWORD size = handle ? GetFileSize(handle, NULL) : INVALID_FILE_SIZE;
        if (size != INVALID_FILE_SIZE) node.size = size;
    }
    node_identity(&node, absolute, &identity);
    identity.inode = path_inode(absolute);
    fill_statx(record, &identity, &node);
    return 0;
}

LONG jolt_file_unlink(const char *absolute, int directory) {
    struct jolt_node node;
    LONG result = jolt_lookup(absolute, &node);
    if (result) return result;
    if (!writable_path(absolute) || node.kind == NODE_VIRTUAL_DIRECTORY) return fail(LINUX_EACCES);
    if (directory && node.kind != NODE_DIRECTORY) return fail(LINUX_ENOTDIR);
    if (!directory && node.kind == NODE_DIRECTORY) return fail(LINUX_EISDIR);
    WCHAR path[MAX_PATH];
    if ((result = host_path(absolute, path))) return result;
    int removed = directory ? RemoveDirectoryW(path) : DeleteFileW(path);
    return removed ? 0 : file_error();
}

LONG jolt_file_mkdir(const char *absolute) {
    struct jolt_node node;
    if (!jolt_lookup(absolute, &node)) return fail(LINUX_EEXIST);
    if (!writable_path(absolute)) return fail(LINUX_EACCES);
    WCHAR path[MAX_PATH];
    LONG result = host_path(absolute, path);
    if (result) return result;
    return CreateDirectoryW(path, NULL) ? 0 : file_error();
}

static LONG replace_directory(const WCHAR *old_path, const WCHAR *new_path, const struct jolt_node *from) {
    if (from->kind != NODE_DIRECTORY) return fail(LINUX_EISDIR);
    if (!RemoveDirectoryW(new_path)) return GetLastError() == ERROR_DIR_NOT_EMPTY ? fail(LINUX_ENOTEMPTY) : file_error();
    return MoveFileW(old_path, new_path) ? 0 : file_error();
}

static LONG replace_file(const WCHAR *old_path, const WCHAR *new_path) {
    WCHAR backup[MAX_PATH];
    if (wcslen(new_path) + wcslen(BACKUP_SUFFIX) >= MAX_PATH) return fail(LINUX_ENAMETOOLONG);
    wcscpy(backup, new_path);
    wcscat(backup, BACKUP_SUFFIX);
    DeleteFileW(backup);
    if (!MoveFileW(new_path, backup)) return file_error();
    if (!MoveFileW(old_path, new_path)) {
        LONG result = file_error();
        MoveFileW(backup, new_path);
        return result;
    }
    DeleteFileW(backup);
    return 0;
}

LONG jolt_file_rename(const char *source, const char *destination) {
    struct jolt_node from, to;
    LONG result = jolt_lookup(source, &from);
    if (result) return result;
    if (!writable_path(source) || !writable_path(destination)) return fail(LINUX_EACCES);
    WCHAR old_path[MAX_PATH], new_path[MAX_PATH];
    if ((result = host_path(source, old_path)) || (result = host_path(destination, new_path))) return result;
    if (!_wcsicmp(old_path, new_path))
        return MoveFileW(old_path, new_path) || !wcscmp(old_path, new_path) ? 0 : file_error();
    if (jolt_lookup(destination, &to)) return MoveFileW(old_path, new_path) ? 0 : file_error();
    if (to.kind == NODE_DIRECTORY) return replace_directory(old_path, new_path, &from);
    if (from.kind == NODE_DIRECTORY) return fail(LINUX_ENOTDIR);
    return replace_file(old_path, new_path);
}

LONG jolt_file_access(const char *absolute, DWORD mode) {
    struct jolt_node node;
    if (mode & ~(DWORD)ACCESS_MASK) return fail(LINUX_EINVAL);
    LONG result = jolt_lookup(absolute, &node);
    if (result) return result;
    int read_only = (node.attributes & FILE_ATTRIBUTE_READONLY) || node.kind == NODE_APPLET ||
        node.kind == NODE_VIRTUAL_DIRECTORY;
    if ((mode & ACCESS_WRITE) && read_only) return fail(LINUX_EACCES);
    if ((mode & ACCESS_EXECUTE) && node.kind == NODE_FILE && !executable_path(absolute)) return fail(LINUX_EACCES);
    return 0;
}

LONG jolt_file_chmod(const char *absolute, DWORD mode) {
    struct jolt_node node;
    LONG result = jolt_lookup(absolute, &node);
    if (result) return result;
    if (node.kind != NODE_FILE && node.kind != NODE_DIRECTORY) return 0;
    if (!writable_path(absolute)) return fail(LINUX_EPERM);
    WCHAR path[MAX_PATH];
    if ((result = host_path(absolute, path))) return result;
    DWORD attributes = node.attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY;
    if (!(mode & 0222)) attributes |= FILE_ATTRIBUTE_READONLY;
    if (!attributes) attributes = FILE_ATTRIBUTE_NORMAL;
    return SetFileAttributesW(path, attributes) ? 0 : file_error();
}

LONG jolt_file_touch(const char *absolute) {
    struct jolt_node node;
    LONG result = jolt_lookup(absolute, &node);
    if (result) return result;
    if (node.kind != NODE_FILE) return 0;
    WCHAR path[MAX_PATH];
    if ((result = host_path(absolute, path))) return result;
    HANDLE handle = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) return file_error();
    SYSTEMTIME now;
    FILETIME time;
    GetSystemTime(&now);
    SystemTimeToFileTime(&now, &time);
    result = SetFileTime(handle, NULL, NULL, &time) ? 0 : file_error();
    CloseHandle(handle);
    return result;
}

LONG jolt_file_statfs(struct linux_statfs64 *record) {
    ULARGE_INTEGER available, total, free;
    if (!GetDiskFreeSpaceExW(root, &available, &total, &free)) return file_error();
    memset(record, 0, sizeof *record);
    record->type = LINUX_MSDOS_SUPER_MAGIC;
    record->block_size = record->fragment_size = BLOCK_SIZE;
    record->blocks = total.QuadPart / BLOCK_SIZE;
    record->free_blocks = free.QuadPart / BLOCK_SIZE;
    record->available_blocks = available.QuadPart / BLOCK_SIZE;
    record->name_length = MAX_PATH - 1;
    return 0;
}

LONG jolt_file_load(const char *absolute, BYTE **data, DWORD *size, DWORD limit) {
    WCHAR path[MAX_PATH];
    LONG result = host_path(absolute, path);
    if (result) return result;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return file_error();
    DWORD length = GetFileSize(file, NULL), count = 0;
    BYTE *buffer = length != INVALID_FILE_SIZE && length <= limit ? LocalAlloc(LMEM_FIXED, length ? length : 1) : NULL;
    int read = buffer && ReadFile(file, buffer, length, &count, NULL) && count == length;
    CloseHandle(file);
    if (!read) {
        if (buffer) LocalFree(buffer);
        return buffer ? fail(LINUX_EIO) : fail(LINUX_ENOMEM);
    }
    *data = buffer;
    *size = length;
    return 0;
}

LONG jolt_file_create(const char *absolute, const void *data, DWORD size) {
    WCHAR path[MAX_PATH];
    LONG result = host_path(absolute, path);
    if (result) return result;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return file_error();
    DWORD written = 0;
    int complete = !size || (WriteFile(file, data, size, &written, NULL) && written == size);
    result = complete ? 0 : file_error();
    CloseHandle(file);
    return result;
}
