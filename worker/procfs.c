#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "procfs.h"
#include "kernel.h"
#include "linux.h"

#define PROC_ROOT "/proc"
#define SELF_LINK "self"
#define EXECUTABLE_TARGET "/bin/busybox"
#define CLOCK_TICKS_PER_SECOND 100
#define MILLISECONDS_PER_TICK (1000 / CLOCK_TICKS_PER_SECOND)
#define PAGE_KILOBYTES 4
#define PAGE_BYTES 4096
#define NAME_CAPACITY 16
#define APM_UNKNOWN 0xff
#define APM_STATUS_HIGH 0
#define APM_STATUS_LOW 1
#define APM_STATUS_CRITICAL 2
#define APM_STATUS_CHARGING 3
#define CE_UNKNOWN_LIFE 0xffffffffUL
#define CE_UNKNOWN_PERCENT 255
#define SECONDS_PER_MINUTE 60

enum proc_file {
    FILE_NONE, FILE_APM, FILE_STAT, FILE_MEMINFO, FILE_UPTIME, FILE_LOADAVG, FILE_VERSION, FILE_CPUINFO, FILE_MOUNTS,
    FILE_PROCESS_STAT, FILE_PROCESS_STATUS, FILE_PROCESS_CMDLINE, FILE_PROCESS_COMM, FILE_PROCESS_EXE
};

struct proc_name {
    const char *name;
    enum proc_file file;
};

static const struct proc_name system_files[] = {
    { "apm", FILE_APM }, { "cpuinfo", FILE_CPUINFO }, { "loadavg", FILE_LOADAVG }, { "meminfo", FILE_MEMINFO },
    { "mounts", FILE_MOUNTS }, { "stat", FILE_STAT }, { "uptime", FILE_UPTIME }, { "version", FILE_VERSION },
};

static const struct proc_name process_files[] = {
    { "cmdline", FILE_PROCESS_CMDLINE }, { "comm", FILE_PROCESS_COMM }, { "exe", FILE_PROCESS_EXE },
    { "stat", FILE_PROCESS_STAT }, { "status", FILE_PROCESS_STATUS },
};

#define SYSTEM_FILE_COUNT (sizeof system_files / sizeof system_files[0])
#define PROCESS_FILE_COUNT (sizeof process_files / sizeof process_files[0])

struct proc_path {
    int root;
    int self_link;
    LONG pid;
    enum proc_file file;
    int valid;
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

static int parse_pid(const char *text, size_t length, LONG *pid) {
    if (!length || length > 6) return 0;
    LONG value = 0;
    for (size_t index = 0; index < length; index++) {
        if (text[index] < '0' || text[index] > '9') return 0;
        value = value * 10 + (text[index] - '0');
    }
    *pid = value;
    return value > 0;
}

int procfs_owns(const char *absolute) {
    size_t length = strlen(PROC_ROOT);
    return !strncmp(absolute, PROC_ROOT, length) && (!absolute[length] || absolute[length] == '/');
}

static int process_exists(LONG pid) {
    int slot = jolt_find_pid(pid);
    return slot >= 0 && kernel()->processes[slot].state != PROCESS_FREE;
}

static struct proc_path parse(const char *absolute) {
    struct proc_path path;
    memset(&path, 0, sizeof path);
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
        path.file = rest ? FILE_NONE : find_file(system_files, SYSTEM_FILE_COUNT, cursor);
        path.valid = path.file != FILE_NONE;
        return path;
    }
    if (!process_exists(path.pid)) return path;
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
    if (!path.root) {
        if (position >= PROCESS_FILE_COUNT) return 0;
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

static LONG render_process_stat(const struct jolt_process *process, char *buffer, DWORD capacity) {
    return snprintf(buffer, capacity,
        "%ld (%s) %c %ld %ld %ld 0 %ld 0 0 0 0 0 0 0 0 0 20 0 1 0 %lu %lu %lu 4294967295 0 0 0 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0\n",
        process->pid, process->name, process_state(process), process->ppid, process->pgid, process->sid,
        kernel()->foreground, process->start_tick / MILLISECONDS_PER_TICK,
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
    if (!GetSystemPowerStatusEx(&power, TRUE)) {
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
    char target[NAME_CAPACITY];
    if (path.self_link) snprintf(target, sizeof target, "%ld", path.pid);
    else if (path.file == FILE_PROCESS_EXE) strcpy(target, EXECUTABLE_TARGET);
    else return -(LONG)LINUX_EINVAL;
    DWORD length = (DWORD)strlen(target);
    if (length > capacity) length = capacity;
    memcpy(buffer, target, length);
    return (LONG)length;
}
