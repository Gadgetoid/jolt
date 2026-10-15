#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "optional.h"
#include "procfs.h"
#include "kernel.h"
#include "linux.h"
#include "text.h"
#include "files.h"

#define CONSOLE_DEVICE ((5 << 8) | 1)
#define PTS_DEVICE_MAJOR 136
#define PROC_ROOT "/proc"
#define SELF_LINK "self"
#define EXECUTABLE_TARGET "/bin/busybox"
#define CLOCK_TICKS_PER_SECOND 100
#define MILLISECONDS_PER_TICK (1000 / CLOCK_TICKS_PER_SECOND)
#define PAGE_KILOBYTES 4
#define PAGE_BYTES 4096
#define NAME_CAPACITY 16
#define TASK_DIRECTORY "task"
#define FD_DIRECTORY "fd"
#define PID_MAX_PATH "sys/kernel/pid_max"
#define PID_MAX 30000
#define APM_UNKNOWN 0xff
#define APM_STATUS_HIGH 0
#define APM_STATUS_LOW 1
#define APM_STATUS_CRITICAL 2
#define APM_STATUS_CHARGING 3
#define CE_UNKNOWN_LIFE 0xffffffffUL
#define CE_UNKNOWN_PERCENT 255
#define SECONDS_PER_MINUTE 60
#define HOST_TEXT_CAPACITY 128
#define OWNER_NAME_LENGTH 36

enum proc_file {
    FILE_NONE, FILE_APM, FILE_JOLT, FILE_STAT, FILE_MEMINFO, FILE_UPTIME, FILE_LOADAVG, FILE_VERSION, FILE_CPUINFO, FILE_MOUNTS,
    FILE_PID_MAX, FILE_PROCESS_STAT, FILE_PROCESS_STATM, FILE_PROCESS_STATUS, FILE_PROCESS_CMDLINE, FILE_PROCESS_COMM,
    FILE_PROCESS_EXE
};

struct proc_name {
    const char *name;
    enum proc_file file;
};

static const struct proc_name system_files[] = {
    { "apm", FILE_APM }, { "cpuinfo", FILE_CPUINFO }, { "jolt", FILE_JOLT }, { "loadavg", FILE_LOADAVG }, { "meminfo", FILE_MEMINFO },
    { "mounts", FILE_MOUNTS }, { "stat", FILE_STAT }, { "uptime", FILE_UPTIME }, { "version", FILE_VERSION },
};

static const struct proc_name process_files[] = {
    { "cmdline", FILE_PROCESS_CMDLINE }, { "comm", FILE_PROCESS_COMM }, { "exe", FILE_PROCESS_EXE },
    { "stat", FILE_PROCESS_STAT }, { "statm", FILE_PROCESS_STATM }, { "status", FILE_PROCESS_STATUS },
};

#define SYSTEM_FILE_COUNT (sizeof system_files / sizeof system_files[0])
#define PROCESS_FILE_COUNT (sizeof process_files / sizeof process_files[0])

struct proc_path {
    int root;
    int self_link;
    LONG pid;
    enum proc_file file;
    int valid;
    int task_listing;
    int fd_listing;
    LONG fd;
};

static struct jolt_kernel *kernel(void) {
    return jolt.kernel;
}

static LONG current_pid(void) {
    DWORD host = GetCurrentProcessId();
    for (int slot = 0; slot < JOLT_PROCESSES; slot++) {
        const struct jolt_process *process = &kernel()->processes[slot];
        if (process->state != PROCESS_FREE && process->host == host) return process->pid;
    }
    return 0;
}

static enum proc_file find_file(const struct proc_name *names, size_t count, const char *name) {
    for (size_t index = 0; index < count; index++)
        if (!strcmp(names[index].name, name)) return names[index].file;
    return FILE_NONE;
}

static int parse_number(const char *text, size_t length, LONG *value) {
    if (!length || length > 6) return 0;
    LONG parsed = 0;
    for (size_t index = 0; index < length; index++) {
        if (text[index] < '0' || text[index] > '9') return 0;
        parsed = parsed * 10 + (text[index] - '0');
    }
    *value = parsed;
    return 1;
}

static int parse_pid(const char *text, size_t length, LONG *pid) {
    return parse_number(text, length, pid) && *pid > 0;
}

int procfs_owns(const char *absolute) {
    size_t length = strlen(PROC_ROOT);
    return !strncmp(absolute, PROC_ROOT, length) && (!absolute[length] || absolute[length] == '/');
}

static int process_exists(LONG pid) {
    int slot = jolt_find_pid(pid);
    return slot >= 0 && kernel()->processes[slot].state != PROCESS_FREE;
}

static int open_description(LONG pid, LONG fd) {
    int slot = jolt_find_pid(pid);
    if (slot < 0 || fd < 0 || fd >= JOLT_FDS) return -1;
    return kernel()->processes[slot].fds[fd] - 1;
}

static struct proc_path parse(const char *absolute) {
    struct proc_path path;
    memset(&path, 0, sizeof path);
    path.fd = -1;
    const char *cursor = absolute + strlen(PROC_ROOT);
    if (!*cursor) {
        path.root = path.valid = 1;
        return path;
    }
    cursor++;
    const char *slash = strchr(cursor, '/');
    size_t length = slash ? (size_t)(slash - cursor) : strlen(cursor);
    const char *rest = slash ? slash + 1 : NULL;
    if (length == strlen(SELF_LINK) && !strncmp(cursor, SELF_LINK, length)) {
        path.pid = current_pid();
        path.self_link = !rest;
    } else if (!parse_pid(cursor, length, &path.pid)) {
        if (!strcmp(cursor, PID_MAX_PATH)) path.file = FILE_PID_MAX;
        else path.file = rest ? FILE_NONE : find_file(system_files, SYSTEM_FILE_COUNT, cursor);
        path.valid = path.file != FILE_NONE;
        return path;
    }
    if (!process_exists(path.pid)) return path;
    if (rest && !strcmp(rest, FD_DIRECTORY)) {
        path.fd_listing = path.valid = 1;
        return path;
    }
    if (rest && !strncmp(rest, FD_DIRECTORY "/", strlen(FD_DIRECTORY) + 1)) {
        const char *number = rest + strlen(FD_DIRECTORY) + 1;
        if (!parse_number(number, strlen(number), &path.fd) || open_description(path.pid, path.fd) < 0) return path;
        path.self_link = path.valid = 1;
        return path;
    }
    if (rest && !strcmp(rest, TASK_DIRECTORY)) {
        path.task_listing = path.valid = 1;
        return path;
    }
    if (rest && !strncmp(rest, TASK_DIRECTORY "/", strlen(TASK_DIRECTORY) + 1)) {
        const char *task = rest + strlen(TASK_DIRECTORY) + 1;
        const char *task_slash = strchr(task, '/');
        size_t task_length = task_slash ? (size_t)(task_slash - task) : strlen(task);
        LONG thread;
        if (!parse_pid(task, task_length, &thread) || thread != path.pid) return path;
        rest = task_slash ? task_slash + 1 : NULL;
    }
    if (rest) {
        path.file = find_file(process_files, PROCESS_FILE_COUNT, rest);
        path.valid = path.file != FILE_NONE;
    } else {
        path.valid = 1;
    }
    return path;
}

enum procfs_kind procfs_lookup(const char *absolute) {
    struct proc_path path = parse(absolute);
    if (!path.valid) return PROCFS_MISSING;
    if (path.self_link) return PROCFS_LINK;
    if (path.root || path.file == FILE_NONE) return PROCFS_DIRECTORY;
    return path.file == FILE_PROCESS_EXE ? PROCFS_EXECUTABLE : PROCFS_FILE;
}

static int nth_live_process(DWORD index, LONG *pid) {
    DWORD seen = 0;
    for (int slot = 0; slot < JOLT_PROCESSES; slot++) {
        const struct jolt_process *process = &kernel()->processes[slot];
        if (process->state == PROCESS_FREE) continue;
        if (seen++ == index) {
            *pid = process->pid;
            return 1;
        }
    }
    return 0;
}

int procfs_entry(const char *directory, DWORD position, char *name, BYTE *type) {
    struct proc_path path = parse(directory);
    if (!path.valid) return 0;
    if (path.fd_listing) {
        int slot = jolt_find_pid(path.pid);
        DWORD seen = 0;
        for (int fd = 0; slot >= 0 && fd < JOLT_FDS; fd++) {
            if (!kernel()->processes[slot].fds[fd] || seen++ != position) continue;
            snprintf(name, NAME_CAPACITY, "%d", fd);
            *type = LINUX_DT_LNK;
            return 1;
        }
        return 0;
    }
    if (path.task_listing) {
        if (position) return 0;
        snprintf(name, NAME_CAPACITY, "%ld", path.pid);
        *type = LINUX_DT_DIR;
        return 1;
    }
    if (!path.root) {
        if (position == PROCESS_FILE_COUNT || position == PROCESS_FILE_COUNT + 1) {
            strcpy(name, position == PROCESS_FILE_COUNT ? TASK_DIRECTORY : FD_DIRECTORY);
            *type = LINUX_DT_DIR;
            return 1;
        }
        if (position > PROCESS_FILE_COUNT + 1) return 0;
        strcpy(name, process_files[position].name);
        *type = LINUX_DT_REG;
        return 1;
    }
    if (position == 0) {
        strcpy(name, SELF_LINK);
        *type = LINUX_DT_DIR;
        return 1;
    }
    position--;
    if (position < SYSTEM_FILE_COUNT) {
        strcpy(name, system_files[position].name);
        *type = LINUX_DT_REG;
        return 1;
    }
    LONG pid;
    if (!nth_live_process(position - SYSTEM_FILE_COUNT, &pid)) return 0;
    snprintf(name, NAME_CAPACITY, "%ld", pid);
    *type = LINUX_DT_DIR;
    return 1;
}

static DWORD uptime_ticks(void) {
    return GetTickCount() / MILLISECONDS_PER_TICK;
}

static void memory_status(MEMORYSTATUS *memory) {
    memset(memory, 0, sizeof *memory);
    memory->dwLength = sizeof *memory;
    GlobalMemoryStatus(memory);
}

static int live_processes(void) {
    int count = 0;
    for (int slot = 0; slot < JOLT_PROCESSES; slot++)
        if (kernel()->processes[slot].state != PROCESS_FREE) count++;
    return count;
}

static char process_state(const struct jolt_process *process) {
    if (process->state == PROCESS_ZOMBIE) return 'Z';
    if (process->stopped) return 'T';
    if (process->waiting != WAIT_NONE) return 'S';
    return 'R';
}

static DWORD terminal_device(LONG ctty) {
    if (ctty == JOLT_NO_TTY) return 0;
    if (ctty == JOLT_CONSOLE) return CONSOLE_DEVICE;
    return (PTS_DEVICE_MAJOR << 8) | (DWORD)(ctty - 1);
}

static LONG render_process_stat(const struct jolt_process *process, char *buffer, DWORD capacity) {
    LONG foreground = process->ctty == JOLT_NO_TTY ? -1 : kernel()->ttys[process->ctty].foreground;
    return snprintf(buffer, capacity,
        "%ld (%s) %c %ld %ld %ld %lu %ld 0 0 0 0 0 0 0 0 0 20 0 1 0 %lu %lu %lu 4294967295 0 0 0 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0\n",
        process->pid, process->name, process_state(process), process->ppid, process->pgid, process->sid,
        terminal_device(process->ctty), foreground, process->start_tick / MILLISECONDS_PER_TICK,
        process->pages * PAGE_BYTES, process->pages);
}

static const char *state_label(char state) {
    switch (state) {
    case 'Z': return "Z (zombie)";
    case 'T': return "T (stopped)";
    case 'S': return "S (sleeping)";
    default: return "R (running)";
    }
}

static LONG render_process_status(const struct jolt_process *process, char *buffer, DWORD capacity) {
    DWORD kilobytes = process->pages * PAGE_KILOBYTES;
    return snprintf(buffer, capacity,
        "Name:\t%s\nState:\t%s\nTgid:\t%ld\nPid:\t%ld\nPPid:\t%ld\nUid:\t0\t0\t0\t0\nGid:\t0\t0\t0\t0\n"
        "VmSize:\t%lu kB\nVmRSS:\t%lu kB\nThreads:\t1\n",
        process->name, state_label(process_state(process)), process->pid, process->pid, process->ppid,
        kilobytes, kilobytes);
}

static LONG render_process(enum proc_file file, LONG pid, char *buffer, DWORD capacity) {
    int slot = jolt_find_pid(pid);
    if (slot < 0) return -(LONG)LINUX_ENOENT;
    const struct jolt_process *process = &kernel()->processes[slot];
    switch (file) {
    case FILE_PROCESS_STAT:
        return render_process_stat(process, buffer, capacity);
    case FILE_PROCESS_STATUS:
        return render_process_status(process, buffer, capacity);
    case FILE_PROCESS_STATM:
        return snprintf(buffer, capacity, "%lu %lu 0 0 0 %lu 0\n", process->pages, process->pages, process->pages);
    case FILE_PROCESS_COMM:
        return snprintf(buffer, capacity, "%s\n", process->name);
    case FILE_PROCESS_CMDLINE: {
        DWORD length = process->command_length < capacity ? process->command_length : capacity;
        memcpy(buffer, process->command, length);
        return (LONG)length;
    }
    default:
        return -(LONG)LINUX_EINVAL;
    }
}

static int apm_status(BYTE flag) {
    if (flag == BATTERY_FLAG_UNKNOWN || (flag & BATTERY_FLAG_NO_BATTERY)) return APM_UNKNOWN;
    if (flag & BATTERY_FLAG_CHARGING) return APM_STATUS_CHARGING;
    if (flag & BATTERY_FLAG_CRITICAL) return APM_STATUS_CRITICAL;
    if (flag & BATTERY_FLAG_LOW) return APM_STATUS_LOW;
    return APM_STATUS_HIGH;
}

static LONG render_apm(char *buffer, DWORD capacity) {
    SYSTEM_POWER_STATUS_EX power;
    memset(&power, 0, sizeof power);
    if (!GetSystemPowerStatusEx(&power, FALSE)) {
        power.ACLineStatus = AC_LINE_UNKNOWN;
        power.BatteryFlag = BATTERY_FLAG_UNKNOWN;
        power.BatteryLifePercent = CE_UNKNOWN_PERCENT;
        power.BatteryLifeTime = CE_UNKNOWN_LIFE;
    }
    int percent = power.BatteryLifePercent == CE_UNKNOWN_PERCENT ? -1 : power.BatteryLifePercent;
    long minutes = power.BatteryLifeTime == CE_UNKNOWN_LIFE ? -1 : (long)(power.BatteryLifeTime / SECONDS_PER_MINUTE);
    int ac = power.ACLineStatus == AC_LINE_UNKNOWN ? APM_UNKNOWN : power.ACLineStatus;
    return snprintf(buffer, capacity, "1.16 1.2 0x03 0x%02x 0x%02x 0x%02x %d%% %ld min\n",
        ac, apm_status(power.BatteryFlag), power.BatteryFlag, percent, minutes);
}

static void put_escaped(char *buffer, DWORD capacity, LONG *used, const char *text) {
    for (; *text; text++) {
        unsigned char character = (unsigned char)*text;
        int escape = character == ' ' || character == '\t' || character == '\n' || character == '\\';
        LONG count = escape ? snprintf(buffer + *used, capacity - *used, "\\%03o", character)
            : snprintf(buffer + *used, capacity - *used, "%c", character);
        if (count < 0 || *used + count >= (LONG)capacity) return;
        *used += count;
    }
}

static LONG render_mounts(char *buffer, DWORD capacity) {
    LONG used = snprintf(buffer, capacity, "rootfs / vfat rw 0 0\nproc /proc proc rw 0 0\n");
    for (int index = 0; index < JOLT_MOUNTS && used < (LONG)capacity; index++) {
        const struct jolt_mount *mount = &kernel()->mounts[index];
        if (!mount->target[0]) continue;
        put_escaped(buffer, capacity, &used, mount->source[0] ? mount->source : "\\");
        LONG count = snprintf(buffer + used, capacity - used, " ");
        if (count > 0 && used + count < (LONG)capacity) used += count;
        put_escaped(buffer, capacity, &used, mount->target);
        count = snprintf(buffer + used, capacity - used, " cefs rw 0 0\n");
        if (count > 0 && used + count < (LONG)capacity) used += count;
    }
    return used;
}

static void host_text(const WCHAR *wide, char *text) {
    DWORD used = jolt_utf8_from_wide(wide, text, HOST_TEXT_CAPACITY - 1);
    text[used] = 0;
}

static void registry_text(HKEY root, const WCHAR *key_name, const WCHAR *value_name, DWORD limit, char *text) {
    WCHAR value[HOST_TEXT_CAPACITY];
    HKEY key;
    DWORD type = 0, size = sizeof value;
    value[0] = 0;
    if (RegOpenKeyExW(root, key_name, 0, 0, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, value_name, NULL, &type, (BYTE *)value, &size) != ERROR_SUCCESS ||
            (type != REG_SZ && type != REG_BINARY))
            value[0] = 0;
        RegCloseKey(key);
    }
    value[limit < HOST_TEXT_CAPACITY ? limit : HOST_TEXT_CAPACITY - 1] = 0;
    host_text(value, text);
}

static void system_parameter_text(UINT action, char *text) {
    WCHAR value[HOST_TEXT_CAPACITY];
    value[0] = 0;
    if (!SystemParametersInfoW(action, sizeof value, value, 0)) value[0] = 0;
    value[HOST_TEXT_CAPACITY - 1] = 0;
    host_text(value, text);
}

typedef BOOL (WINAPI *store_function)(LPSTORE_INFORMATION);

static LONG render_jolt(char *buffer, DWORD capacity) {
    char oem[HOST_TEXT_CAPACITY], platform[HOST_TEXT_CAPACITY], device[HOST_TEXT_CAPACITY], owner[HOST_TEXT_CAPACITY];
    OSVERSIONINFOW version;
    STORE_INFORMATION store;
    memset(&version, 0, sizeof version);
    version.dwOSVersionInfoSize = sizeof version;
    GetVersionExW(&version);
    memset(&store, 0, sizeof store);
    store_function read_store = (store_function)jolt_optional(L"GetStoreInformation");
    if (read_store) read_store(&store);
    system_parameter_text(SPI_GETOEMINFO, oem);
    system_parameter_text(SPI_GETPLATFORMTYPE, platform);
    registry_text(HKEY_LOCAL_MACHINE, L"Ident", L"Name", HOST_TEXT_CAPACITY, device);
    registry_text(HKEY_CURRENT_USER, L"ControlPanel\\Owner", L"Owner", OWNER_NAME_LENGTH, owner);
    struct linux_statfs64 card;
    memset(&card, 0, sizeof card);
    jolt_file_statfs(&card);
    DWORD card_total_kb = (DWORD)(card.blocks * card.block_size / 1024);
    DWORD card_free_kb = (DWORD)(card.available_blocks * card.block_size / 1024);
    HDC screen = GetDC(NULL);
    int depth = screen ? GetDeviceCaps(screen, BITSPIXEL) * GetDeviceCaps(screen, PLANES) : 0;
    if (screen) ReleaseDC(NULL, screen);
    return snprintf(buffer, capacity,
        "version: %s\nce_version: %lu.%02lu\nce_build: %lu\noem: %s\nplatform: %s\ndevice: %s\nowner: %s\n"
        "display: %dx%dx%d\nstore_kb: %lu\nstore_free_kb: %lu\ncard_kb: %lu\ncard_free_kb: %lu\n"
        "terminal: %ux%u\n",
        JOLT_VERSION, version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber, oem, platform,
        device, owner, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), depth,
        store.dwStoreSize / 1024, store.dwFreeSize / 1024, card_total_kb, card_free_kb,
        kernel()->ttys[JOLT_CONSOLE].columns, kernel()->ttys[JOLT_CONSOLE].rows);
}

static LONG render_system(enum proc_file file, char *buffer, DWORD capacity) {
    MEMORYSTATUS memory;
    DWORD ticks = uptime_ticks();
    switch (file) {
    case FILE_STAT:
        return snprintf(buffer, capacity,
            "cpu  0 0 0 %lu 0 0 0 0 0 0\ncpu0 0 0 0 %lu 0 0 0 0 0 0\nbtime 0\nprocesses %d\nprocs_running 1\nprocs_blocked 0\n",
            ticks, ticks, live_processes());
    case FILE_MEMINFO:
        memory_status(&memory);
        return snprintf(buffer, capacity,
            "MemTotal:\t%lu kB\nMemFree:\t%lu kB\nMemAvailable:\t%lu kB\nBuffers:\t0 kB\nCached:\t0 kB\n"
            "SwapTotal:\t0 kB\nSwapFree:\t0 kB\n",
            memory.dwTotalPhys / 1024, memory.dwAvailPhys / 1024, memory.dwAvailPhys / 1024);
    case FILE_UPTIME:
        return snprintf(buffer, capacity, "%lu.%02lu %lu.%02lu\n", ticks / CLOCK_TICKS_PER_SECOND,
            ticks % CLOCK_TICKS_PER_SECOND, ticks / CLOCK_TICKS_PER_SECOND, ticks % CLOCK_TICKS_PER_SECOND);
    case FILE_LOADAVG:
        return snprintf(buffer, capacity, "0.00 0.00 0.00 1/%d %ld\n", live_processes(), kernel()->next_pid - 1);
    case FILE_VERSION:
        return snprintf(buffer, capacity, "Linux version 0.2-jolt (JoLT on Windows CE 3.0)\n");
    case FILE_CPUINFO:
        return snprintf(buffer, capacity,
            "Processor\t: StrongARM-1110 rev 9 (v4l)\nBogoMIPS\t: 206.00\nFeatures\t: swp half\n"
            "Hardware\t: HP Jornada 720\n");
    case FILE_MOUNTS:
        return render_mounts(buffer, capacity);
    case FILE_APM:
        return render_apm(buffer, capacity);
    case FILE_PID_MAX:
        return snprintf(buffer, capacity, "%d\n", PID_MAX);
    case FILE_JOLT:
        return render_jolt(buffer, capacity);
    default:
        return -(LONG)LINUX_EINVAL;
    }
}

LONG procfs_render(const char *absolute, char *buffer, DWORD capacity) {
    struct proc_path path = parse(absolute);
    if (!path.valid || path.file == FILE_NONE) return -(LONG)LINUX_EISDIR;
    LONG length = path.pid ? render_process(path.file, path.pid, buffer, capacity) : render_system(path.file, buffer, capacity);
    if (length > (LONG)capacity) length = (LONG)capacity;
    return length;
}

LONG procfs_readlink(const char *absolute, char *buffer, DWORD capacity) {
    struct proc_path path = parse(absolute);
    if (!path.valid) return -(LONG)LINUX_ENOENT;
    char target[JOLT_GUEST_PATH];
    if (path.fd >= 0) {
        const struct jolt_description *description = &kernel()->descriptions[open_description(path.pid, path.fd)];
        if (description->path[0]) snprintf(target, sizeof target, "%s", description->path);
        else if (description->type == DESCRIPTION_PIPE_READ || description->type == DESCRIPTION_PIPE_WRITE)
            snprintf(target, sizeof target, "pipe:[%lu]", (unsigned long)description->pipe);
        else snprintf(target, sizeof target, "anon_inode:[%lu]", (unsigned long)description->type);
    } else if (path.self_link) snprintf(target, sizeof target, "%ld", path.pid);
    else if (path.file == FILE_PROCESS_EXE) strcpy(target, EXECUTABLE_TARGET);
    else return -(LONG)LINUX_EINVAL;
    DWORD length = (DWORD)strlen(target);
    if (length > capacity) length = capacity;
    memcpy(buffer, target, length);
    return (LONG)length;
}
