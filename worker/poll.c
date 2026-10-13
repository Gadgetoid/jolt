#include <windows.h>
#include <string.h>
#include "worker.h"
#include "memory.h"
#include "process.h"
#include "syscalls.h"
#include "network.h"
#include "serial.h"

#define SET_WORDS ((JOLT_FDS + 31) / 32)
#define SOCKET_POLL_MILLISECONDS 50

struct descriptor_sets {
    DWORD read[SET_WORDS], write[SET_WORDS], except[SET_WORDS];
};

static int pipe_input_readiness(const struct jolt_pipe *pipe) {
    int ready = 0;
    if (pipe->written != pipe->read) ready |= LINUX_POLLIN;
    if (pipe->writers <= 0) ready |= LINUX_POLLIN | LINUX_POLLHUP;
    return ready;
}

static int pipe_output_readiness(const struct jolt_pipe *pipe) {
    if (pipe->readers <= 0) return LINUX_POLLERR;
    return pipe->written - pipe->read < JOLT_PIPE_SIZE ? LINUX_POLLOUT : 0;
}

static int readiness(DWORD fd, int events) {
    int index = fd_description(fd);
    if (index < 0) return LINUX_POLLNVAL;
    const struct jolt_description *description = description_at(index);
    struct jolt_kernel *k = kernel();
    int ready = 0;
    if (description->type == DESCRIPTION_TTY) {
        if ((DWORD)k->input_written != (DWORD)k->input_read || k->input_eof) ready |= LINUX_POLLIN;
        ready |= LINUX_POLLOUT;
    } else if (description->type == DESCRIPTION_PIPE_READ) {
        ready = pipe_input_readiness(&k->pipes[description->pipe]);
    } else if (description->type == DESCRIPTION_STREAM) {
        ready = pipe_input_readiness(&k->pipes[description->pipe]);
        if (!(description->shutdown & JOLT_SHUTDOWN_WRITE))
            ready |= pipe_output_readiness(&k->pipes[description->output]);
    } else if (description->type == DESCRIPTION_SOCKET) {
        ready = network_readiness(index, events);
    } else if (description->type == DESCRIPTION_SERIAL) {
        ready = serial_readiness(index);
    } else if (description->type == DESCRIPTION_PIPE_WRITE) {
        ready = pipe_output_readiness(&k->pipes[description->pipe]);
    } else {
        ready = LINUX_POLLIN | LINUX_POLLOUT;
    }
    return ready & (events | LINUX_POLLERR | LINUX_POLLHUP | LINUX_POLLNVAL);
}

static int is_socket(DWORD fd) {
    int index = fd_description(fd);
    return index >= 0 && description_at(index)->type == DESCRIPTION_SOCKET;
}

static LONG wait_or_interrupt(DWORD began, DWORD timeout, int sockets) {
    if (signal_ready()) {
        self->waiting = WAIT_NONE;
        jolt_unlock();
        return error(LINUX_EINTR);
    }
    self->waiting = WAIT_POLL;
    jolt_unlock();
    DWORD left = remaining(began, timeout);
    block_until(sockets && left > SOCKET_POLL_MILLISECONDS ? SOCKET_POLL_MILLISECONDS : left);
    return 0;
}

static LONG poll_descriptors(DWORD pointer, DWORD count, DWORD timeout) {
    struct linux_pollfd *entries = (struct linux_pollfd *)pointer;
    if (count > JOLT_FDS || !guest_writable(pointer, count * sizeof *entries)) return error(LINUX_EFAULT);
    DWORD began = GetTickCount();
    for (;;) {
        LONG ready = 0;
        int sockets = 0;
        jolt_lock();
        for (DWORD index = 0; index < count; index++) {
            struct linux_pollfd *entry = &entries[index];
            if (entry->fd >= 0 && is_socket((DWORD)entry->fd)) sockets = 1;
            entry->returned_events = entry->fd < 0 ? 0 : (SHORT)readiness((DWORD)entry->fd, entry->events);
            if (entry->returned_events) ready++;
        }
        if (ready || !remaining(began, timeout)) {
            self->waiting = WAIT_NONE;
            jolt_unlock();
            return ready;
        }
        LONG interrupted = wait_or_interrupt(began, timeout, sockets);
        if (interrupted) return interrupted;
    }
}

static LONG poll_with_mask(const DWORD *arguments, int wide) {
    DWORD descriptors = arguments[0], count = arguments[1], timeout_pointer = arguments[2], mask_pointer = arguments[3];
    LONG failure;
    DWORD timeout = timespec_milliseconds(timeout_pointer, wide, &failure);
    if (failure) return failure;
    if (mask_pointer) {
        DWORD mask;
        if (!guest_copy_in(&mask, mask_pointer, sizeof mask)) return error(LINUX_EFAULT);
        suspend_signals(mask);
    }
    LONG result = poll_descriptors(descriptors, count, timeout);
    if (mask_pointer && result != error(LINUX_EINTR)) resume_signals();
    return result;
}

LONG sys_poll(const DWORD *arguments) {
    LONG timeout = (LONG)arguments[2];
    return poll_descriptors(arguments[0], arguments[1], timeout < 0 ? INFINITE : (DWORD)timeout);
}

LONG sys_ppoll(const DWORD *arguments) {
    return poll_with_mask(arguments, 0);
}

LONG sys_ppoll_time64(const DWORD *arguments) {
    return poll_with_mask(arguments, 1);
}

static int load_set(DWORD *set, DWORD pointer, DWORD words) {
    memset(set, 0, SET_WORDS * sizeof(DWORD));
    if (!pointer) return 1;
    return guest_copy_in(set, pointer, words * sizeof(DWORD)) && guest_writable(pointer, words * sizeof(DWORD));
}

static int is_member(const DWORD *set, DWORD fd) {
    return (set[fd / 32] >> (fd % 32)) & 1;
}

static void add_member(DWORD *set, DWORD fd) {
    set[fd / 32] |= 1UL << (fd % 32);
}

static LONG collect_ready(const struct descriptor_sets *wanted, struct descriptor_sets *found, DWORD count, int *sockets) {
    LONG ready = 0;
    *sockets = 0;
    for (DWORD fd = 0; fd < count; fd++) {
        int reading = is_member(wanted->read, fd), writing = is_member(wanted->write, fd);
        if (!reading && !writing && !is_member(wanted->except, fd)) continue;
        if (is_socket(fd)) *sockets = 1;
        int events = (reading ? LINUX_POLLIN : 0) | (writing ? LINUX_POLLOUT : 0);
        int state_bits = readiness(fd, events);
        if (state_bits & LINUX_POLLNVAL) return error(LINUX_EBADF);
        if (reading && (state_bits & (LINUX_POLLIN | LINUX_POLLHUP | LINUX_POLLERR))) {
            add_member(found->read, fd);
            ready++;
        }
        if (writing && (state_bits & (LINUX_POLLOUT | LINUX_POLLERR))) {
            add_member(found->write, fd);
            ready++;
        }
    }
    return ready;
}

static LONG select_descriptors(DWORD count, DWORD read_set, DWORD write_set, DWORD except_set, DWORD timeout) {
    if (count > JOLT_FDS) count = JOLT_FDS;
    DWORD words = (count + 31) / 32;
    struct descriptor_sets wanted;
    if (!load_set(wanted.read, read_set, words) || !load_set(wanted.write, write_set, words) ||
        !load_set(wanted.except, except_set, words)) return error(LINUX_EFAULT);
    DWORD began = GetTickCount();
    for (;;) {
        struct descriptor_sets found;
        memset(&found, 0, sizeof found);
        int sockets;
        jolt_lock();
        LONG ready = collect_ready(&wanted, &found, count, &sockets);
        if (ready < 0) {
            jolt_unlock();
            return ready;
        }
        if (ready || !remaining(began, timeout)) {
            self->waiting = WAIT_NONE;
            jolt_unlock();
            if (read_set) guest_copy_out(read_set, found.read, words * sizeof(DWORD));
            if (write_set) guest_copy_out(write_set, found.write, words * sizeof(DWORD));
            if (except_set) guest_copy_out(except_set, found.except, words * sizeof(DWORD));
            return ready;
        }
        LONG interrupted = wait_or_interrupt(began, timeout, sockets);
        if (interrupted) return interrupted;
    }
}

LONG sys_newselect(const DWORD *arguments) {
    DWORD timeout = INFINITE, timeout_pointer = arguments[4];
    if (timeout_pointer) {
        struct linux_timeval value;
        if (!guest_copy_in(&value, timeout_pointer, sizeof value)) return error(LINUX_EFAULT);
        timeout = (DWORD)value.seconds * 1000 + (DWORD)value.microseconds / 1000;
    }
    return select_descriptors(arguments[0], arguments[1], arguments[2], arguments[3], timeout);
}

static LONG pselect(const DWORD *arguments, int wide) {
    LONG failure;
    DWORD timeout = timespec_milliseconds(arguments[4], wide, &failure);
    if (failure) return failure;
    return select_descriptors(arguments[0], arguments[1], arguments[2], arguments[3], timeout);
}

LONG sys_pselect6(const DWORD *arguments) {
    return pselect(arguments, 0);
}

LONG sys_pselect6_time64(const DWORD *arguments) {
    return pselect(arguments, 1);
}
