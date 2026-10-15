#include <windows.h>
#include <string.h>
#include <wchar.h>
#include "worker.h"
#include "memory.h"
#include "process.h"

#define PID_LIMIT 30000
#define FORK_POLL_MILLISECONDS 200
#define TOTAL_PAGES (PAGE_WORDS * 32)


static int free_slot(void) {
    for (int index = 0; index < JOLT_PROCESSES; index++)
        if (kernel()->processes[index].state == PROCESS_FREE) return index;
    return -1;
}

static LONG next_pid(int child) {
    struct jolt_kernel *k = kernel();
    LONG pid;
    do {
        pid = k->next_pid++;
        if (k->next_pid > PID_LIMIT) k->next_pid = 2;
        k->processes[child].pid = pid;
    } while (jolt_find_pid(pid) != child);
    return pid;
}

static void prepare_child(int child) {
    struct jolt_process *process = &kernel()->processes[child];
    memset(process, 0, sizeof *process);
    process->state = PROCESS_STARTING;
    process->start = START_FORK;
    process->source = slot;
    next_pid(child);
    process->ppid = self->pid;
    process->pgid = self->pgid;
    process->sid = self->sid;
    process->ctty = self->ctty;
    process->fatal = self->fatal;
    process->ignored = self->ignored;
    memcpy(process->fds, self->fds, sizeof process->fds);
    memcpy(process->cloexec, self->cloexec, sizeof process->cloexec);
    memcpy(process->name, self->name, sizeof process->name);
    memcpy(process->command, self->command, sizeof process->command);
    process->command_length = self->command_length;
    process->pages = self->pages;
    process->start_tick = GetTickCount();
    for (int fd = 0; fd < JOLT_FDS; fd++)
        if (process->fds[fd]) kernel()->descriptions[process->fds[fd] - 1].references++;
}

static void abandon_child(struct jolt_process *process) {
    for (int fd = 0; fd < JOLT_FDS; fd++) jolt_fd_close(process, fd);
    process->state = PROCESS_FREE;
}

static void slot_command(WCHAR *command, int child) {
    wsprintfW(command, L"--ipc=%08lx --slot=%d", jolt.identifier, child);
}

static int claim_spare(int child, PROCESS_INFORMATION *information) {
    struct jolt_kernel *k = kernel();
    jolt_lock();
    int ready = k->spare_state == SPARE_READY;
    if (ready) {
        k->spare_state = SPARE_ASSIGNED;
        k->spare_slot = child;
        information->hProcess = OpenProcess(0, FALSE, k->spare_host);
    }
    jolt_unlock();
    if (!ready) return 0;
    SetEvent(jolt.spare);
    SetEvent(jolt.refill);
    return 1;
}

static int start_child(int child, PROCESS_INFORMATION *information) {
    memset(information, 0, sizeof *information);
    if (claim_spare(child, information)) return 1;
    WCHAR command[64];
    slot_command(command, child);
    return CreateProcessW(worker_path, command, NULL, NULL, FALSE, 0, NULL, NULL, NULL, information) != 0;
}

static LONG await_child(struct jolt_process *process, HANDLE host, LONG pid) {
    for (;;) {
        jolt_lock();
        DWORD current = process->state;
        jolt_unlock();
        if (current == PROCESS_RUNNING || current == PROCESS_ZOMBIE) return pid;
        if (current != PROCESS_STARTING) return error(LINUX_ENOMEM);
        HANDLE handles[2] = { jolt.wake[slot], host };
        DWORD signalled = WaitForMultipleObjects(host ? 2 : 1, handles, FALSE, FORK_POLL_MILLISECONDS);
        if (signalled != WAIT_OBJECT_0 + 1) continue;
        jolt_lock();
        int failed = process->state == PROCESS_STARTING;
        if (failed) abandon_child(process);
        jolt_unlock();
        return failed ? error(LINUX_ENOMEM) : pid;
    }
}

LONG worker_fork(void) {
    jolt_lock();
    int child = free_slot();
    if (child >= 0) prepare_child(child);
    jolt_unlock();
    if (child < 0) return error(LINUX_EAGAIN);
    struct jolt_process *process = &kernel()->processes[child];
    LONG pid = process->pid;
    DWORD began = GetTickCount();
    PROCESS_INFORMATION information;
    if (!start_child(child, &information)) {
        jolt_lock();
        abandon_child(process);
        jolt_unlock();
        return error(LINUX_EAGAIN);
    }
    LONG result = await_child(process, information.hProcess, pid);
    if (information.hThread) CloseHandle(information.hThread);
    if (information.hProcess) CloseHandle(information.hProcess);
    trace("fork child %ld result %ld in %lu ms\r\n", pid, result, GetTickCount() - began);
    return result;
}

static int page_present(const DWORD *pages, DWORD page) {
    return (pages[page / 32] >> (page % 32)) & 1;
}

static int page_is_text(DWORD page) {
    DWORD address = GUEST_START + page * PAGE_SIZE;
    return state.image_shared && address >= state.text_start && address < state.text_end;
}

static int copy_run(HANDLE source, DWORD first, DWORD last, int text) {
    DWORD address = GUEST_START + first * PAGE_SIZE, size = (last - first + 1) * PAGE_SIZE, count = 0;
    const BYTE *shared = text ? image_shared_data() + state.text_offset + (address - state.text_start) : NULL;
    if (text && alias_pages(address, size, shared)) return 1;
    if (!commit_pages(address, size)) return 0;
    if (text) {
        memcpy((void *)address, shared, size);
        return 1;
    }
    return ReadProcessMemory(source, (void *)address, (void *)address, size, &count) && count == size;
}

static int copy_pages(HANDLE source, const DWORD *pages) {
    for (DWORD page = 0; page < TOTAL_PAGES; page++) {
        if (!page_present(pages, page)) continue;
        int text = page_is_text(page);
        DWORD last = page;
        while (last + 1 < TOTAL_PAGES && page_present(pages, last + 1) && page_is_text(last + 1) == text) last++;
        if (!copy_run(source, page, last, text)) return 0;
        page = last;
    }
    return 1;
}

int fork_child(void) {
    DWORD began = GetTickCount();
    HANDLE source = OpenProcess(0, FALSE, kernel()->processes[self->source].host);
    if (!source) return 0;
    DWORD count = 0;
    int success = ReadProcessMemory(source, &state, &state, sizeof state, &count) && count == sizeof state;
    DWORD pages[PAGE_WORDS];
    memcpy(pages, state.committed, sizeof pages);
    memset(state.committed, 0, sizeof state.committed);
    state.page_count = 0;
    if (success) image_attach();
    success = success && copy_pages(source, pages);
    CloseHandle(source);
    for (DWORD index = 0; success && index < state.range_count; index++) success = protect_range(&state.ranges[index]);
    DWORD child_result = 0;
    if (!success || !guest_copy_out(state.context.guest_frame, &child_result, sizeof child_result)) return 0;
    trace("fork copy %lu pages in %lu ms\r\n", state.page_count, GetTickCount() - began);
    state.context.exit_requested = EXIT_NONE;
    state.context.syscall_count = 0;
    return 1;
}
