#include "kernel.h"
#include <string.h>
#include <wchar.h>

#define IMAGE_KEY_DIGITS 8

struct jolt_runtime jolt;

void jolt_object_name(WCHAR *name, const WCHAR *prefix, DWORD identifier, int index) {
    static const WCHAR digits[] = L"0123456789abcdef";
    wcscpy(name, prefix);
    size_t length = wcslen(name);
    for (unsigned int position = 0; position < 8; position++)
        name[length++] = digits[(identifier >> ((7 - position) * 4)) & 15];
    if (index >= 0) {
        name[length++] = L'-';
        if (index >= 10) name[length++] = (WCHAR)(L'0' + index / 10);
        name[length++] = (WCHAR)(L'0' + index % 10);
    }
    name[length] = 0;
}

int jolt_kernel_open(DWORD identifier, int create, DWORD image_size) {
    WCHAR name[64];
    jolt.identifier = identifier;
    jolt_object_name(name, L"Jolt-kernel-", identifier, -1);
    jolt.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(struct jolt_kernel), name);
    if (!jolt.mapping) return 0;
    int existed = GetLastError() == ERROR_ALREADY_EXISTS;
    if (create == existed) return 0;
    jolt.kernel = MapViewOfFile(jolt.mapping, FILE_MAP_WRITE, 0, 0, sizeof(struct jolt_kernel));
    if (!jolt.kernel) return 0;
    if (create) {
        memset(jolt.kernel, 0, sizeof *jolt.kernel);
        jolt.kernel->magic = JOLT_KERNEL_MAGIC;
        jolt.kernel->version = JOLT_KERNEL_VERSION;
        jolt.kernel->size = sizeof *jolt.kernel;
        jolt.kernel->image_size = image_size;
        jolt.kernel->next_pid = 1;
    } else if (jolt.kernel->magic != JOLT_KERNEL_MAGIC || jolt.kernel->version != JOLT_KERNEL_VERSION ||
        jolt.kernel->size != sizeof *jolt.kernel) return 0;
    jolt_object_name(name, L"Jolt-lock-", identifier, -1);
    jolt.lock = CreateMutexW(NULL, FALSE, name);
    if (!jolt.lock) return 0;
    for (int slot = 0; slot < JOLT_PROCESSES; slot++) {
        jolt_object_name(name, L"Jolt-wake-", identifier, slot);
        jolt.wake[slot] = CreateEventW(NULL, FALSE, FALSE, name);
        if (!jolt.wake[slot]) return 0;
    }
    jolt_object_name(name, L"Jolt-spare-", identifier, -1);
    jolt.spare = CreateEventW(NULL, FALSE, FALSE, name);
    jolt_object_name(name, L"Jolt-refill-", identifier, -1);
    jolt.refill = CreateEventW(NULL, FALSE, FALSE, name);
    if (!jolt.spare || !jolt.refill) return 0;
    jolt_object_name(name, L"Jolt-network-", identifier, -1);
    jolt.network = CreateEventW(NULL, FALSE, FALSE, name);
    if (!jolt.network) return 0;
    jolt_object_name(name, L"Jolt-serial-", identifier, -1);
    jolt.serial = CreateEventW(NULL, FALSE, FALSE, name);
    if (!jolt.serial) return 0;
    jolt_object_name(name, L"Jolt-image-", identifier, -1);
    jolt.image_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, jolt.kernel->image_size, name);
    if (!jolt.image_mapping || (GetLastError() == ERROR_ALREADY_EXISTS) == create) return 0;
    jolt.image = MapViewOfFile(jolt.image_mapping, FILE_MAP_WRITE, 0, 0, jolt.kernel->image_size);
    return jolt.image != NULL;
}

void jolt_kernel_close(void) {
    if (jolt.image) UnmapViewOfFile((void *)jolt.image);
    if (jolt.image_mapping) CloseHandle(jolt.image_mapping);
    for (int slot = 0; slot < JOLT_PROCESSES; slot++) if (jolt.wake[slot]) CloseHandle(jolt.wake[slot]);
    if (jolt.lock) CloseHandle(jolt.lock);
    if (jolt.spare) CloseHandle(jolt.spare);
    if (jolt.refill) CloseHandle(jolt.refill);
    if (jolt.network) CloseHandle(jolt.network);
    if (jolt.serial) CloseHandle(jolt.serial);
    if (jolt.kernel) UnmapViewOfFile(jolt.kernel);
    if (jolt.mapping) CloseHandle(jolt.mapping);
    memset(&jolt, 0, sizeof jolt);
}

void jolt_lock(void) {
    WaitForSingleObject(jolt.lock, INFINITE);
}

void jolt_unlock(void) {
    ReleaseMutex(jolt.lock);
}

void jolt_wake(int slot) {
    if (slot >= 0 && slot < JOLT_PROCESSES) SetEvent(jolt.wake[slot]);
}

void jolt_wake_all(void) {
    for (int slot = 0; slot < JOLT_PROCESSES; slot++)
        if (jolt.kernel->processes[slot].state != PROCESS_FREE) SetEvent(jolt.wake[slot]);
}

void jolt_wake_object(LONG object) {
    for (int slot = 0; slot < JOLT_PROCESSES; slot++) {
        const struct jolt_process *process = &jolt.kernel->processes[slot];
        if (process->state == PROCESS_FREE || process->waiting == WAIT_NONE) continue;
        if (process->wait_object == JOLT_WAIT_ANY || process->wait_object == object) SetEvent(jolt.wake[slot]);
    }
}

int jolt_find_pid(LONG pid) {
    for (int slot = 0; slot < JOLT_PROCESSES; slot++) {
        struct jolt_process *process = &jolt.kernel->processes[slot];
        if (process->state != PROCESS_FREE && process->pid == pid) return slot;
    }
    return -1;
}

void jolt_notify_parent(int slot) {
    struct jolt_process *process = &jolt.kernel->processes[slot];
    int parent = process->ppid ? jolt_find_pid(process->ppid) : -1;
    if (parent < 0 || jolt.kernel->processes[parent].state != PROCESS_RUNNING) return;
    jolt_signal_process(parent, LINUX_SIGCHLD);
    jolt_wake(parent);
}

static void continue_process(int slot) {
    struct jolt_process *process = &jolt.kernel->processes[slot];
    process->pending &= ~LINUX_DEFAULT_STOP;
    if (process->stopped) {
        process->stopped = 0;
        process->continued = 1;
        jolt_notify_parent(slot);
    }
    jolt_wake(slot);
}

void jolt_signal_process(int slot, int signal) {
    struct jolt_process *process = &jolt.kernel->processes[slot];
    if (signal < 1 || signal > LINUX_SIGNAL_LIMIT) return;
    if (process->state != PROCESS_RUNNING && process->state != PROCESS_STARTING) return;
    DWORD bit = LINUX_SIGNAL_BIT(signal);
    if (signal == LINUX_SIGCONT) continue_process(slot);
    if (bit & LINUX_DEFAULT_STOP) process->pending &= ~LINUX_SIGNAL_BIT(LINUX_SIGCONT);
    if (signal != LINUX_SIGKILL && (process->ignored & bit)) return;
    process->pending |= bit;
    if ((signal == LINUX_SIGKILL || (process->fatal & bit)) && !process->signal_time)
        process->signal_time = GetTickCount() | 1;
    jolt_wake(slot);
}

void jolt_signal_group(LONG group, int signal) {
    for (int slot = 0; slot < JOLT_PROCESSES; slot++) {
        struct jolt_process *process = &jolt.kernel->processes[slot];
        if (process->state != PROCESS_FREE && process->pgid == group) jolt_signal_process(slot, signal);
    }
}

int jolt_description_alloc(DWORD type, DWORD flags, const char *path) {
    for (int index = 0; index < JOLT_DESCRIPTIONS; index++) {
        struct jolt_description *description = &jolt.kernel->descriptions[index];
        if (description->type != DESCRIPTION_FREE) continue;
        DWORD generation = description->generation + 1;
        memset(description, 0, sizeof *description);
        description->references = 1;
        description->type = type;
        description->flags = flags;
        description->generation = generation;
        if (path) {
            strncpy(description->path, path, JOLT_GUEST_PATH - 1);
            description->path[JOLT_GUEST_PATH - 1] = 0;
        }
        return index;
    }
    return -1;
}

static void commit_clipboard(const struct jolt_description *description) {
    if (description->type != DESCRIPTION_FILE) return;
    if (strncmp(description->path, JOLT_CLIPBOARD_PREFIX, sizeof JOLT_CLIPBOARD_PREFIX - 1)) return;
    if ((description->flags & LINUX_O_ACCMODE) == LINUX_O_RDONLY) return;
    strcpy(jolt.kernel->clipboard_commit, description->path);
    if (jolt.kernel->window) PostMessageW((HWND)jolt.kernel->window, JOLT_CLIPBOARD_MESSAGE, 0, 0);
}

void jolt_release_device(DWORD type, LONG tty, DWORD port) {
    if (type == DESCRIPTION_SERIAL && --jolt.kernel->serial[port].users <= 0) SetEvent(jolt.serial);
    if (type == DESCRIPTION_TTY) jolt_tty_release_end(tty, 0);
    if (type == DESCRIPTION_PTY_MASTER) jolt_tty_release_end(tty, 1);
}

void jolt_description_release(int index) {
    if (index < 0 || index >= JOLT_DESCRIPTIONS) return;
    struct jolt_description *description = &jolt.kernel->descriptions[index];
    if (description->type == DESCRIPTION_FREE || --description->references > 0) return;
    if (description->type == DESCRIPTION_PIPE_READ) jolt.kernel->pipes[description->pipe].readers--;
    if (description->type == DESCRIPTION_PIPE_WRITE) jolt.kernel->pipes[description->pipe].writers--;
    commit_clipboard(description);
    jolt_release_device(description->type, description->tty, description->pipe);
    if (description->type == DESCRIPTION_STREAM) {
        if (!(description->shutdown & JOLT_SHUTDOWN_READ)) jolt.kernel->pipes[description->pipe].readers--;
        if (!(description->shutdown & JOLT_SHUTDOWN_WRITE)) jolt.kernel->pipes[description->output].writers--;
    }
    description->type = DESCRIPTION_FREE;
    description->generation++;
    jolt_wake_all();
}

int jolt_fd_install(struct jolt_process *process, int minimum, int description, int cloexec) {
    for (int fd = minimum < 0 ? 0 : minimum; fd < JOLT_FDS; fd++) {
        if (process->fds[fd]) continue;
        process->fds[fd] = (BYTE)(description + 1);
        if (cloexec) process->cloexec[fd / 32] |= 1UL << (fd % 32);
        else process->cloexec[fd / 32] &= ~(1UL << (fd % 32));
        return fd;
    }
    return -24;
}

void jolt_fd_close(struct jolt_process *process, int fd) {
    if (fd < 0 || fd >= JOLT_FDS || !process->fds[fd]) return;
    int description = process->fds[fd] - 1;
    process->fds[fd] = 0;
    process->cloexec[fd / 32] &= ~(1UL << (fd % 32));
    jolt_description_release(description);
}

void jolt_process_exit(int slot, DWORD status) {
    struct jolt_kernel *kernel = jolt.kernel;
    struct jolt_process *process = &kernel->processes[slot];
    if (process->state == PROCESS_FREE || process->state == PROCESS_ZOMBIE) return;
    for (int fd = 0; fd < JOLT_FDS; fd++) jolt_fd_close(process, fd);
    process->state = PROCESS_ZOMBIE;
    process->status = status;
    process->waiting = WAIT_NONE;
    process->pending = 0;
    int init = jolt_find_pid(1);
    int init_alive = init >= 0 && kernel->processes[init].state == PROCESS_RUNNING && process->pid != 1;
    for (int index = 0; index < JOLT_PROCESSES; index++) {
        struct jolt_process *child = &kernel->processes[index];
        if (child->state != PROCESS_FREE && index != slot && child->ppid == process->pid) {
            child->ppid = init_alive ? 1 : 0;
            if (init_alive) jolt_wake(init);
        }
    }
    int parent = process->ppid ? jolt_find_pid(process->ppid) : -1;
    if (parent >= 0 && kernel->processes[parent].state == PROCESS_RUNNING) jolt_notify_parent(slot);
    else process->ppid = 0;
    if (kernel->window && !InterlockedExchange(&kernel->ttys[JOLT_CONSOLE].output_notified, 1))
        PostMessageW((HWND)kernel->window, JOLT_TTY_MESSAGE, 0, 0);
}


void jolt_image_name(WCHAR *name, DWORD key) {
    static const WCHAR digits[] = L"0123456789abcdef";
    jolt_object_name(name, L"Jolt-app-", jolt.identifier, -1);
    size_t length = wcslen(name);
    name[length++] = L'-';
    for (int position = IMAGE_KEY_DIGITS - 1; position >= 0; position--) name[length++] = digits[(key >> (position * 4)) & 15];
    name[length] = 0;
}

void jolt_image_touch(DWORD key, DWORD size) {
    struct jolt_kernel *kernel = jolt.kernel;
    jolt_lock();
    struct jolt_warm *slot = &kernel->warm[0];
    for (int index = 0; index < JOLT_WARM_IMAGES; index++) {
        struct jolt_warm *entry = &kernel->warm[index];
        if (entry->key == key) {
            slot = entry;
            break;
        }
        if (entry->used < slot->used) slot = entry;
    }
    slot->key = key;
    slot->size = size;
    slot->used = ++kernel->warm_clock;
    jolt_unlock();
}

LONG jolt_image_used(DWORD key) {
    for (int index = 0; index < JOLT_WARM_IMAGES; index++)
        if (jolt.kernel->warm[index].key == key) return jolt.kernel->warm[index].used;
    return 0;
}
