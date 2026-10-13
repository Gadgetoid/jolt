#include <windows.h>
#include <string.h>
#include <wchar.h>
#include "broker.h"
#include "session.h"
#include "tty.h"
#include "kernel.h"
#include "digest.h"
#include "network.h"
#include "sound.h"
#include "serial.h"
#include "guest-manifest.h"
#include <jornada/sdk/wincrypt.h>

#define IMAGE_FILE L"busybox.elf"
#define WORKER_FILE L"JoltWorker.exe"
#define INIT_PID 1
#define INIT_SLOT 0
#define TERMINATED_EXIT_CODE 137
#define FATAL_SIGNAL_GRACE_MILLISECONDS 1500
#define SPARE_RETRY_MILLISECONDS 1000
#define SPARE_POLL_MILLISECONDS 1000
#define SPARE_STOP_MILLISECONDS 5000
#define COMMAND_CAPACITY 64
#define DIGEST_SIZE 32

#define INIT_PATH "/bin/sh"
#ifndef PROV_RSA_FULL
#define PROV_RSA_FULL 1
#endif
#ifndef CRYPT_VERIFYCONTEXT
#define CRYPT_VERIFYCONTEXT 0xF0000000UL
#endif
#define LOGIN_NAME "-sh"
#define COMMAND_OPTION "-c"
#define HOLD_SUFFIX "; exec sh"

static const char init_environment[] =
    "HOME=/\0PATH=/bin:/usr/bin\0TERM=xterm-256color\0SHELL=/bin/sh\0USER=root\0LOGNAME=root\0\0";

struct supervised_host {
    HANDLE handle;
    DWORD id;
    int killed;
};

static struct supervised_host hosts[JOLT_PROCESSES];
static HANDLE spare_process, spare_thread;
static volatile int spare_running = 1;

static struct jolt_kernel *kernel(void) {
    return jolt.kernel;
}

static int fail(const char *message) {
    strncpy(session.message, message, MESSAGE_CAPACITY - 1);
    return 0;
}

int broker_load_image(void) {
    WCHAR path[MAX_PATH];
    session_path(path, IMAGE_FILE);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return fail("FAIL: busybox.elf missing");
    DWORD count = 0;
    int read = GetFileSize(file, NULL) == JOLT_GUEST_SIZE &&
        ReadFile(file, (void *)jolt.image, JOLT_GUEST_SIZE, &count, NULL) && count == JOLT_GUEST_SIZE;
    CloseHandle(file);
    if (!read) return fail("FAIL: busybox.elf wrong size");
    unsigned char digest[DIGEST_SIZE];
    jolt_sha256(jolt.image, JOLT_GUEST_SIZE, digest);
    if (memcmp(digest, jolt_guest_sha256, sizeof digest)) return fail("REFUSED: guest SHA-256 differs.");
    return 1;
}

static void worker_command(WCHAR *command, const WCHAR *role) {
    jolt_object_name(command, L"--ipc=", jolt.identifier, -1);
    wcscat(command, L" ");
    wcscat(command, role);
}

static int create_worker(const WCHAR *role, PROCESS_INFORMATION *information) {
    WCHAR path[MAX_PATH], command[COMMAND_CAPACITY];
    session_path(path, WORKER_FILE);
    worker_command(command, role);
    if (!CreateProcessW(path, command, NULL, NULL, FALSE, 0, NULL, NULL, NULL, information)) return 0;
    CloseHandle(information->hThread);
    return 1;
}

static char *put_string(char *cursor, const char *text) {
    size_t length = strlen(text);
    memcpy(cursor, text, length);
    return cursor + length;
}

static void write_init_arguments(char *arguments) {
    char *cursor = put_string(arguments, INIT_PATH);
    *cursor++ = 0;
    cursor = put_string(cursor, LOGIN_NAME);
    *cursor++ = 0;
    if (session.command[0]) {
        cursor = put_string(cursor, COMMAND_OPTION);
        *cursor++ = 0;
        cursor = put_string(cursor, session.command);
        if (session.hold) cursor = put_string(cursor, HOLD_SUFFIX);
        *cursor++ = 0;
    }
    *cursor++ = 0;
    memcpy(cursor, init_environment, sizeof init_environment);
}

static void seed_entropy(BYTE *entropy) {
    HCRYPTPROV provider;
    if (CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        BOOL filled = CryptGenRandom(provider, JOLT_ENTROPY_SIZE, entropy);
        CryptReleaseContext(provider, 0);
        if (filled) return;
    }
    DWORD mix[JOLT_ENTROPY_SIZE / sizeof(DWORD)];
    MEMORYSTATUS memory;
    memory.dwLength = sizeof memory;
    GlobalMemoryStatus(&memory);
    for (DWORD index = 0; index < JOLT_ENTROPY_SIZE / sizeof(DWORD); index++)
        mix[index] = GetTickCount() ^ GetCurrentProcessId() ^ memory.dwAvailPhys ^ (index * 2654435761UL);
    jolt_sha256((const BYTE *)mix, sizeof mix, entropy);
}

static void prepare_init(void) {
    struct jolt_kernel *k = kernel();
    seed_entropy(k->entropy);
    write_init_arguments(k->arguments);
    struct jolt_process *init = &k->processes[INIT_SLOT];
    memset(init, 0, sizeof *init);
    init->state = PROCESS_STARTING;
    init->start = START_EXEC;
    init->pid = init->pgid = init->sid = k->foreground = INIT_PID;
    k->next_pid = INIT_PID + 1;
    init->fatal = ~(LINUX_DEFAULT_IGNORED | LINUX_DEFAULT_STOP);
    init->ignored = LINUX_DEFAULT_IGNORED;
    int terminal = jolt_description_alloc(DESCRIPTION_TTY, LINUX_O_RDWR, "/dev/tty");
    k->descriptions[terminal].references = 3;
    init->fds[0] = init->fds[1] = init->fds[2] = (BYTE)(terminal + 1);
}

static DWORD WINAPI keep_spare_ready(LPVOID unused) {
    (void)unused;
    while (spare_running) {
        jolt_lock();
        int needed = spare_running && kernel()->spare_state == SPARE_NONE;
        if (needed) kernel()->spare_state = SPARE_STARTING;
        jolt_unlock();
        PROCESS_INFORMATION information;
        if (needed && create_worker(L"--spare", &information)) {
            HANDLE previous = spare_process;
            spare_process = information.hProcess;
            if (previous) CloseHandle(previous);
        } else if (needed) {
            jolt_lock();
            kernel()->spare_state = SPARE_NONE;
            jolt_unlock();
            Sleep(SPARE_RETRY_MILLISECONDS);
        }
        WaitForSingleObject(jolt.refill, SPARE_POLL_MILLISECONDS);
    }
    return 0;
}

int broker_start(void) {
    prepare_init();
    PROCESS_INFORMATION information;
    if (!create_worker(L"--slot=0", &information)) return fail("FAIL: JoltWorker.exe could not start");
    hosts[INIT_SLOT].handle = information.hProcess;
    hosts[INIT_SLOT].id = information.dwProcessId;
    spare_thread = CreateThread(NULL, 0, keep_spare_ready, NULL, 0, NULL);
    network_start();
    sound_start();
    serial_start();
    return 1;
}

static void stop_spare(void) {
    jolt_lock();
    spare_running = 0;
    kernel()->spare_state = SPARE_ASSIGNED;
    jolt_unlock();
    SetEvent(jolt.refill);
    if (spare_thread) {
        WaitForSingleObject(spare_thread, SPARE_STOP_MILLISECONDS);
        CloseHandle(spare_thread);
        spare_thread = NULL;
    }
    if (spare_process) {
        TerminateProcess(spare_process, TERMINATED_EXIT_CODE);
        CloseHandle(spare_process);
        spare_process = NULL;
    }
}

static int is_live(const struct jolt_process *process) {
    return process->state == PROCESS_RUNNING || process->state == PROCESS_STARTING;
}

static void release_host(struct supervised_host *host) {
    if (host->handle) CloseHandle(host->handle);
    memset(host, 0, sizeof *host);
}

void broker_stop(void) {
    stop_spare();
    network_stop();
    sound_stop();
    serial_stop();
    for (int slot = 0; slot < JOLT_PROCESSES; slot++) {
        const struct jolt_process *process = &kernel()->processes[slot];
        struct supervised_host *host = &hosts[slot];
        if (is_live(process) && process->host) {
            int cached = host->handle && host->id == process->host;
            HANDLE handle = cached ? host->handle : OpenProcess(0, FALSE, process->host);
            if (handle) TerminateProcess(handle, TERMINATED_EXIT_CODE);
            if (handle && !cached) CloseHandle(handle);
        }
        release_host(host);
    }
}

static void track_host(int slot, const struct jolt_process *process) {
    struct supervised_host *host = &hosts[slot];
    if (!process->host || host->id == process->host) return;
    release_host(host);
    host->handle = OpenProcess(0, FALSE, process->host);
    host->id = process->host;
}

static int fatal_signal_overdue(const struct jolt_process *process) {
    if (!process->signal_time) return 0;
    if (process->pending & LINUX_SIGNAL_BIT(LINUX_SIGKILL)) return 1;
    return (process->pending & process->fatal) && GetTickCount() - process->signal_time > FATAL_SIGNAL_GRACE_MILLISECONDS;
}

static void supervise(int slot) {
    struct jolt_process *process = &kernel()->processes[slot];
    struct supervised_host *host = &hosts[slot];
    if (is_live(process)) {
        track_host(slot, process);
        if (host->handle && WaitForSingleObject(host->handle, 0) == WAIT_OBJECT_0) {
            jolt_process_exit(slot, host->killed ? LINUX_SIGKILL : LINUX_SIGSEGV);
        } else if (host->handle && !host->killed && fatal_signal_overdue(process)) {
            TerminateProcess(host->handle, TERMINATED_EXIT_CODE);
            host->killed = 1;
        }
    }
    if (process->state == PROCESS_ZOMBIE && process->ppid == 0 && process->pid != INIT_PID) process->state = PROCESS_FREE;
    if ((process->state == PROCESS_FREE || process->state == PROCESS_ZOMBIE) && host->handle) release_host(host);
}

void broker_monitor(void) {
    jolt_lock();
    for (int slot = 0; slot < JOLT_PROCESSES; slot++) supervise(slot);
    int init = jolt_find_pid(INIT_PID);
    if (!session.finished && (init < 0 || kernel()->processes[init].state == PROCESS_ZOMBIE)) {
        session_finish(init >= 0 ? kernel()->processes[init].status : LINUX_SIGKILL, NULL);
        if (init >= 0) kernel()->processes[init].state = PROCESS_FREE;
    }
    jolt_unlock();
}

int broker_idle(DWORD quiet_interval) {
    struct jolt_kernel *k = kernel();
    if (tty_line_pending() || (DWORD)k->input_written != (DWORD)k->input_read) return 0;
    if ((DWORD)k->tty_written != (DWORD)k->tty_read) return 0;
    if (GetTickCount() - session.last_output_time < quiet_interval) return 0;
    int reading = 0, starting = 0;
    jolt_lock();
    for (int slot = 0; slot < JOLT_PROCESSES; slot++) {
        const struct jolt_process *process = &k->processes[slot];
        if (process->state == PROCESS_STARTING) starting = 1;
        int waiting_input = process->waiting == WAIT_TTY || process->waiting == WAIT_POLL;
        int foreground = process->pgid == k->foreground;
        if (process->state == PROCESS_RUNNING && foreground && waiting_input) reading = 1;
    }
    jolt_unlock();
    return reading && !starting;
}
