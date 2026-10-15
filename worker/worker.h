#pragma once
#include <windows.h>
#include "kernel.h"
#include "linux.h"
#include "bridge.h"

#define GUEST_RESERVE_BASE 0x00c00000UL
#define GUEST_RESERVE_LIMIT 0x01400000UL
#define GUEST_START 0x00c10000UL
#define IMAGE_LIMIT 0x01000000UL
#define MMAP_START 0x01000000UL
#define GATE_PAGE 0x013c0000UL
#define STACK_BASE 0x013d0000UL
#define STACK_SIZE 0x00010000UL
#define GUEST_LIMIT 0x013e0000UL
#define PAGE_SIZE 4096UL
#define FNV_OFFSET 2166136261UL
#define FNV_PRIME 16777619UL
#define MAX_RANGES 512
#define MAX_PAGES 2000
#define MAX_SAVED_SIGNALS 8
#define PAGE_WORDS (((GUEST_LIMIT - GUEST_START) / PAGE_SIZE + 31UL) / 32UL)
#define IO_LIMIT 0x100000UL

#define RANGE_EXECUTE 1
#define RANGE_WRITE 2
#define RANGE_READ 4

enum exit_request { EXIT_NONE, EXIT_PROCESS, EXIT_EXEC };

struct guest_range {
    DWORD start, end, flags;
};

struct signal_action {
    DWORD handler, flags, restorer, mask;
};

struct saved_signal {
    DWORD frame;
    struct jolt_registers registers;
    DWORD flags;
    DWORD blocked;
};

struct worker_state {
    struct jolt_context context;
    struct guest_range ranges[MAX_RANGES];
    DWORD range_count;
    DWORD committed[PAGE_WORDS];
    DWORD page_count;
    DWORD heap_start, heap_break, heap_end;
    int heap_range;
    DWORD blocked;
    int suspended;
    DWORD suspend_saved;
    struct signal_action actions[LINUX_SIGNAL_LIMIT + 1];
    struct saved_signal saved[MAX_SAVED_SIGNALS];
    DWORD saved_count;
    char cwd[JOLT_GUEST_PATH];
    DWORD umask;
    DWORD entry, stack;
    DWORD exit_status;
    DWORD random;
    DWORD text_start, text_end, text_offset;
    int image_shared;
    DWORD image_key, image_mapping_size;
    DWORD alarm_deadline;
    DWORD alarm_interval;
};

extern struct worker_state state;
extern int slot;
extern struct jolt_process *self;
extern WCHAR worker_path[MAX_PATH];

void trace(const char *format, ...);
DWORD next_random(void);

static inline struct jolt_kernel *kernel(void) {
    return jolt.kernel;
}

static inline int fd_description(DWORD fd) {
    if (fd >= JOLT_FDS || !self->fds[fd]) return -1;
    return self->fds[fd] - 1;
}

static inline struct jolt_description *description_at(int index) {
    return &jolt.kernel->descriptions[index];
}

static inline void check_alarm(void) {
    if (!state.alarm_deadline || (LONG)(GetTickCount() - state.alarm_deadline) < 0) return;
    jolt_signal_process(slot, LINUX_SIGALRM);
    state.alarm_deadline = state.alarm_interval ? GetTickCount() + state.alarm_interval : 0;
}

static inline int signal_ready(void) {
    check_alarm();
    return (self->pending & ~state.blocked) != 0;
}

static inline void block_until(DWORD timeout) {
    if (state.alarm_deadline) {
        LONG left = (LONG)(state.alarm_deadline - GetTickCount());
        DWORD until_alarm = left > 0 ? (DWORD)left : 0;
        if (until_alarm < timeout) timeout = until_alarm;
    }
    WaitForSingleObject(jolt.wake[slot], timeout);
    self->wait_object = JOLT_WAIT_ANY;
}

static inline DWORD remaining(DWORD began, DWORD timeout) {
    if (timeout == INFINITE) return INFINITE;
    DWORD elapsed = GetTickCount() - began;
    return elapsed >= timeout ? 0 : timeout - elapsed;
}

static inline LONG error(enum linux_errno code) {
    return -(LONG)code;
}
