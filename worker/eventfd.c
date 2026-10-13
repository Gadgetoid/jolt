#include <string.h>
#include <windows.h>
#include "worker.h"
#include "memory.h"
#include "syscalls.h"
#include "eventfd.h"

#define EVENTFD_SEMAPHORE 1
#define EVENTFD_LIMIT 0xfffffffffffffffeULL

static LONG create(DWORD initial, DWORD flags) {
    if (flags & ~(DWORD)(EVENTFD_SEMAPHORE | LINUX_O_NONBLOCK | LINUX_O_CLOEXEC)) return error(LINUX_EINVAL);
    DWORD stored = LINUX_O_RDWR | (flags & LINUX_O_NONBLOCK) | (flags & EVENTFD_SEMAPHORE ? JOLT_EVENTFD_SEMAPHORE : 0);
    jolt_lock();
    int index = jolt_description_alloc(DESCRIPTION_EVENT, stored, "anon_inode:[eventfd]");
    LONG fd = index < 0 ? error(LINUX_ENFILE) : jolt_fd_install(self, 0, index, (flags & LINUX_O_CLOEXEC) != 0);
    if (index >= 0) {
        description_at(index)->counter = initial;
        if (fd < 0) jolt_description_release(index);
    }
    jolt_unlock();
    return fd;
}

LONG sys_eventfd(const DWORD *arguments) {
    return create(arguments[0], 0);
}

LONG sys_eventfd2(const DWORD *arguments) {
    return create(arguments[0], arguments[1]);
}

LONG eventfd_read(int index, char *buffer, DWORD length) {
    struct jolt_description *description = description_at(index);
    if (length < sizeof(ULONGLONG)) return error(LINUX_EINVAL);
    for (;;) {
        jolt_lock();
        if (description->counter) {
            ULONGLONG value = description->flags & JOLT_EVENTFD_SEMAPHORE ? 1 : description->counter;
            description->counter -= value;
            self->waiting = WAIT_NONE;
            jolt_wake_all();
            jolt_unlock();
            memcpy(buffer, &value, sizeof value);
            return sizeof value;
        }
        LONG result = 1;
        if (description->flags & LINUX_O_NONBLOCK) result = error(LINUX_EAGAIN);
        else if (signal_ready()) result = error(LINUX_EINTR);
        if (result <= 0) {
            self->waiting = WAIT_NONE;
            jolt_unlock();
            return result;
        }
        self->waiting = WAIT_PIPE;
        jolt_unlock();
        block_until(INFINITE);
    }
}

LONG eventfd_write(int index, const char *buffer, DWORD length) {
    struct jolt_description *description = description_at(index);
    ULONGLONG value;
    if (length < sizeof value) return error(LINUX_EINVAL);
    memcpy(&value, buffer, sizeof value);
    if (value == ~0ULL) return error(LINUX_EINVAL);
    for (;;) {
        jolt_lock();
        if (description->counter <= EVENTFD_LIMIT - value) {
            description->counter += value;
            self->waiting = WAIT_NONE;
            jolt_wake_all();
            jolt_unlock();
            return sizeof value;
        }
        LONG result = 1;
        if (description->flags & LINUX_O_NONBLOCK) result = error(LINUX_EAGAIN);
        else if (signal_ready()) result = error(LINUX_EINTR);
        if (result <= 0) {
            self->waiting = WAIT_NONE;
            jolt_unlock();
            return result;
        }
        self->waiting = WAIT_PIPE;
        jolt_unlock();
        block_until(INFINITE);
    }
}

int eventfd_readiness(int index) {
    const struct jolt_description *description = description_at(index);
    int ready = description->counter ? LINUX_POLLIN : 0;
    if (description->counter < EVENTFD_LIMIT) ready |= LINUX_POLLOUT;
    return ready;
}
