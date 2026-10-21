#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "worker.h"
#include "memorymap.h"
#include "memory.h"
#include "process.h"
#include "files.h"

#define START_ARGUMENT_LIMIT 63
#define EXIT_STATUS_NOT_STARTED LINUX_WAIT_EXITED(127)
#define TRACE_LINE_CAPACITY 256

struct worker_state state;
int slot = -1;
struct jolt_process *self;
WCHAR worker_path[MAX_PATH];

static HANDLE log_file = INVALID_HANDLE_VALUE;
static char start_strings[JOLT_ARGUMENTS];
static char *start_arguments[START_ARGUMENT_LIMIT + 1];
static char *start_environment[START_ARGUMENT_LIMIT + 1];

void trace(const char *format, ...) {
    if (log_file == INVALID_HANDLE_VALUE) return;
    char text[TRACE_LINE_CAPACITY];
    int stamp = snprintf(text, sizeof text, "%lu ", (unsigned long)GetTickCount());
    if (stamp < 0) return;
    va_list list;
    va_start(list, format);
    int length = vsnprintf(text + stamp, sizeof text - stamp, format, list);
    va_end(list);
    if (length < 0) return;
    length += stamp;
    if (length >= (int)sizeof text) length = sizeof text - 1;
    DWORD written = 0;
    WriteFile(log_file, text, (DWORD)length, &written, NULL);
}

static char *split_list(char *cursor, char **list) {
    unsigned int count = 0;
    while (cursor < start_strings + sizeof start_strings && *cursor && count < START_ARGUMENT_LIMIT) {
        list[count++] = cursor;
        cursor += strlen(cursor) + 1;
    }
    list[count] = NULL;
    return cursor + 1;
}

static int start_initial_program(void) {
    memcpy(start_strings, jolt.kernel->arguments, sizeof start_strings);
    start_strings[sizeof start_strings - 1] = 0;
    char *path = start_strings;
    char *cursor = split_list(path + strlen(path) + 1, start_arguments);
    split_list(cursor, start_environment);
    strcpy(state.cwd, "/");
    state.umask = 022;
    state.random = GetTickCount() ^ (DWORD)slot << 16;
    state.heap_range = -1;
    struct jolt_node node;
    if (jolt_lookup(path, &node) || node.kind != NODE_APPLET) return 0;
    struct guest_image image;
    image_use_shared(&image);
    return worker_exec(path, start_arguments, start_environment, &image) == 0;
}

static int parse_number(const WCHAR *text, const WCHAR *key, int base, DWORD *value) {
    const WCHAR *found = wcsstr(text, key);
    if (!found) return 0;
    WCHAR *end = NULL;
    *value = wcstoul(found + wcslen(key), &end, base);
    return end != found + wcslen(key);
}

static void diagnose(const char *format, ...) {
    va_list list;
    va_start(list, format);
    vsnprintf(jolt.kernel->diagnostic, JOLT_DIAGNOSTIC, format, list);
    va_end(list);
}

static int probe_commit(void) {
    DWORD probe = STACK_LIMIT - PAGE_SIZE;
    if (VirtualAlloc((void *)probe, PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE) != (void *)probe) return 0;
    VirtualFree((void *)probe, PAGE_SIZE, MEM_DECOMMIT);
    return 1;
}

static DWORD slot_address(const void *address) {
    return (DWORD)address & SLOT_MASK;
}

static int guest_layout_available(int report, const WCHAR *folder) {
    volatile BYTE stack_probe = 0;
    if (!guest_reserve(guest_reserve_ceiling()) && !guest_reserve(GUEST_RESERVE_LIMIT)) {
        MEMORY_BASIC_INFORMATION area;
        memset(&area, 0, sizeof area);
        VirtualQuery((void *)GUEST_RESERVE_BASE, &area, sizeof area);
        if (!report) return 0;
        DWORD error = GetLastError();
        memory_map_write(folder);
        diagnose("worker: cannot reserve guest memory %08lx-%08lx (error %lu, base %08lx state %lx type %lx, map in memory.txt)",
            GUEST_RESERVE_BASE, GUEST_RESERVE_LIMIT, error, (DWORD)area.AllocationBase, area.State, area.Type);
        return 0;
    }
    DWORD code = slot_address((const void *)WinMain), data = slot_address(&state), stack = slot_address((const void *)&stack_probe);
    if (code >= GUEST_RESERVE_BASE || data >= GUEST_RESERVE_BASE || stack >= GUEST_RESERVE_BASE) {
        if (report) diagnose("worker: host memory overlaps the guest range (code %08lx data %08lx stack %08lx)", code, data,
            stack);
        return 0;
    }
    if (!probe_commit()) {
        if (report) diagnose("worker: cannot commit guest memory at %08lx (error %lu)", STACK_LIMIT - PAGE_SIZE, GetLastError());
        return 0;
    }
    return 1;
}

static int wait_as_spare(DWORD *assigned_slot) {
    HANDLE launcher = OpenProcess(0, FALSE, jolt.kernel->launcher);
    if (!launcher) return 0;
    jolt_lock();
    jolt.kernel->spare_host = GetCurrentProcessId();
    jolt.kernel->spare_state = SPARE_READY;
    jolt_unlock();
    int assigned = 0;
    while (!assigned) {
        HANDLE handles[2] = { jolt.spare, launcher };
        if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0) break;
        jolt_lock();
        assigned = jolt.kernel->spare_state == SPARE_ASSIGNED && jolt.kernel->spare_host == GetCurrentProcessId();
        if (assigned) {
            *assigned_slot = (DWORD)jolt.kernel->spare_slot;
            jolt.kernel->spare_state = SPARE_NONE;
            jolt.kernel->spare_host = 0;
        }
        jolt_unlock();
    }
    CloseHandle(launcher);
    return assigned;
}

static void open_trace(const WCHAR *folder) {
    if (!jolt.kernel->trace) return;
    WCHAR path[MAX_PATH];
    wsprintfW(path, L"%sJoltTrace-%ld.log", folder, self->pid);
    log_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

static void report_start(int started, int resumed) {
    jolt_lock();
    if (started) {
        self->state = PROCESS_RUNNING;
        if (resumed) jolt_wake(self->source);
    } else if (resumed) {
        for (int fd = 0; fd < JOLT_FDS; fd++) jolt_fd_close(self, fd);
        self->state = PROCESS_FREE;
        jolt_wake(self->source);
    } else {
        jolt_process_exit(slot, EXIT_STATUS_NOT_STARTED);
    }
    jolt_unlock();
}

static void run_guest(int resumed) {
    for (;;) {
        state.context.exit_requested = EXIT_NONE;
        if (resumed) jolt_resume(&state.context);
        else jolt_enter(state.entry, state.stack, &state.context);
        if (state.context.exit_requested != EXIT_EXEC) break;
        resumed = 0;
    }
    jolt_lock();
    for (int fd = 0; fd < JOLT_FDS; fd++) close_local(fd);
    jolt_unlock();
    release_memory();
    jolt_files_forget();
    jolt_lock();
    jolt_process_exit(slot, state.exit_status);
    jolt_unlock();
    trace("exit status %04lx syscalls %lu\r\n", state.exit_status, state.context.syscall_count);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show) {
    (void)instance;
    (void)previous;
    (void)show;
    DWORD identifier = 0, assigned_slot = 0;
    int spare = command_line && wcsstr(command_line, L"--spare") != NULL;
    if (!command_line || !parse_number(command_line, L"--ipc=", 16, &identifier)) return 1;
    if (!spare && (!parse_number(command_line, L"--slot=", 10, &assigned_slot) || assigned_slot >= JOLT_PROCESSES)) return 1;
    if (!jolt_kernel_open(identifier, 0, 0)) return 1;
    WCHAR folder[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, worker_path, MAX_PATH);
    WCHAR *filename = length && length < MAX_PATH - 32 ? wcsrchr(worker_path, L'\\') : NULL;
    if (!filename) return 1;
    wcscpy(folder, worker_path);
    folder[filename - worker_path + 1] = 0;
    int layout = guest_layout_available(!spare, folder);
    int ready = layout && jolt_files_init(folder);
    if (layout && !ready && !spare) diagnose("worker: cannot open the JoLT folder");
    if (spare && (!ready || !wait_as_spare(&assigned_slot))) {
        jolt_kernel_close();
        return 0;
    }
    slot = (int)assigned_slot;
    self = &jolt.kernel->processes[slot];
    open_trace(folder);
    jolt_lock();
    self->host = GetCurrentProcessId();
    jolt_unlock();
    int resumed = self->start == START_FORK;
    int started = ready && (resumed ? fork_child() : start_initial_program());
    trace("start slot %d pid %ld started %d\r\n", slot, self->pid, started);
    if (ready && !started && !resumed) diagnose("worker: could not start the first shell from busybox.elf");
    report_start(started, resumed);
    if (started) run_guest(resumed);
    if (log_file != INVALID_HANDLE_VALUE) CloseHandle(log_file);
    jolt_kernel_close();
    return started ? 0 : 1;
}
