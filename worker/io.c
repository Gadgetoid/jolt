#include <windows.h>
#include <string.h>
#include "worker.h"
#include "memory.h"
#include "process.h"
#include "syscalls.h"
#include "files.h"
#include "procfs.h"
#include "network.h"
#include "random.h"
#include "serial.h"
#include "eventfd.h"

#define VECTOR_LIMIT 64

static LONG pipe_read(int index, char *buffer, DWORD length) {
    struct jolt_description *description = description_at(index);
    struct jolt_pipe *pipe = &kernel()->pipes[description->pipe];
    for (;;) {
        jolt_lock();
        DWORD available = pipe->written - pipe->read;
        if (available) {
            DWORD count = available < length ? available : length;
            for (DWORD offset = 0; offset < count; offset++)
                buffer[offset] = pipe->data[(pipe->read + offset) % JOLT_PIPE_SIZE];
            pipe->read += count;
            self->waiting = WAIT_NONE;
            jolt_wake_all();
            jolt_unlock();
            return (LONG)count;
        }
        LONG result = 1;
        if (pipe->writers <= 0) result = 0;
        else if (description->flags & LINUX_O_NONBLOCK) result = error(LINUX_EAGAIN);
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

static DWORD pipe_space(const struct jolt_pipe *pipe) {
    return JOLT_PIPE_SIZE - (pipe->written - pipe->read);
}

static DWORD pipe_put(struct jolt_pipe *pipe, const char *buffer, DWORD length) {
    DWORD space = pipe_space(pipe);
    int atomic = length <= JOLT_PIPE_SIZE;
    if (!space || (atomic && space < length)) return 0;
    DWORD count = length < space ? length : space;
    for (DWORD offset = 0; offset < count; offset++)
        pipe->data[(pipe->written + offset) % JOLT_PIPE_SIZE] = buffer[offset];
    pipe->written += count;
    return count;
}

static LONG pipe_write(int index, DWORD pipe_number, const char *buffer, DWORD length) {
    struct jolt_description *description = description_at(index);
    struct jolt_pipe *pipe = &kernel()->pipes[pipe_number];
    DWORD done = 0;
    for (;;) {
        jolt_lock();
        if (pipe->readers <= 0 || (description->shutdown & JOLT_SHUTDOWN_WRITE)) {
            jolt_signal_process(slot, LINUX_SIGPIPE);
            jolt_unlock();
            return done ? (LONG)done : error(LINUX_EPIPE);
        }
        DWORD count = pipe_put(pipe, buffer + done, length - done);
        if (count) jolt_wake_all();
        done += count;
        LONG stop = 0;
        if (done == length) stop = (LONG)done;
        else if (description->flags & LINUX_O_NONBLOCK) stop = done ? (LONG)done : error(LINUX_EAGAIN);
        else if (signal_ready()) stop = done ? (LONG)done : error(LINUX_EINTR);
        if (stop) {
            self->waiting = WAIT_NONE;
            jolt_unlock();
            return stop;
        }
        self->waiting = WAIT_PIPE;
        jolt_unlock();
        block_until(INFINITE);
    }
}

static LONG locked_file_read(int index, void *buffer, DWORD length, LONG position) {
    jolt_lock();
    LONG result = jolt_file_read(index, buffer, length, position);
    jolt_unlock();
    return result;
}

static LONG locked_file_write(int index, const void *buffer, DWORD length, LONG position) {
    jolt_lock();
    LONG result = jolt_file_write(index, buffer, length, position);
    jolt_unlock();
    return result;
}

#define PROC_RENDER_CAPACITY 4096

static LONG read_proc(struct jolt_description *description, char *buffer, DWORD length, LONG position) {
    static char rendered[PROC_RENDER_CAPACITY];
    LONG size = procfs_render(description->path, rendered, sizeof rendered);
    if (size < 0) return size;
    DWORD offset = position >= 0 ? (DWORD)position : description->offset;
    if (offset >= (DWORD)size) return 0;
    DWORD count = (DWORD)size - offset < length ? (DWORD)size - offset : length;
    memcpy(buffer, rendered + offset, count);
    if (position < 0) description->offset = offset + count;
    return (LONG)count;
}

static LONG read_descriptor(DWORD fd, DWORD buffer, DWORD length, LONG position) {
    int index = fd_description(fd);
    if (index < 0) return error(LINUX_EBADF);
    if (length > IO_LIMIT) length = IO_LIMIT;
    if (!guest_writable(buffer, length)) return error(LINUX_EFAULT);
    struct jolt_description *description = description_at(index);
    if ((description->flags & LINUX_O_ACCMODE) == LINUX_O_WRONLY) return error(LINUX_EBADF);
    int nonblocking = (description->flags & LINUX_O_NONBLOCK) != 0;
    switch (description->type) {
    case DESCRIPTION_TTY:
        return tty_read(description->tty, (char *)buffer, length, nonblocking);
    case DESCRIPTION_PTY_MASTER:
        return pty_master_read(description->tty, (char *)buffer, length, nonblocking);
    case DESCRIPTION_PIPE_READ:
    case DESCRIPTION_STREAM:
        return pipe_read(index, (char *)buffer, length);
    case DESCRIPTION_NULL:
        return 0;
    case DESCRIPTION_ZERO:
        memset((void *)buffer, 0, length);
        return (LONG)length;
    case DESCRIPTION_DIRECTORY:
        return error(LINUX_EISDIR);
    case DESCRIPTION_FILE:
        return locked_file_read(index, (void *)buffer, length, position);
    case DESCRIPTION_PROC:
        return read_proc(description, (char *)buffer, length, position);
    case DESCRIPTION_SOCKET:
        return network_read(index, (char *)buffer, length);
    case DESCRIPTION_SERIAL:
        return serial_read(index, (char *)buffer, length, nonblocking);
    case DESCRIPTION_EVENT:
        return eventfd_read(index, (char *)buffer, length);
    case DESCRIPTION_RANDOM:
        random_fill((BYTE *)buffer, length);
        return (LONG)length;
    default:
        return error(LINUX_EBADF);
    }
}

static LONG write_descriptor(DWORD fd, DWORD buffer, DWORD length, LONG position) {
    int index = fd_description(fd);
    if (index < 0) return error(LINUX_EBADF);
    if (length > IO_LIMIT) length = IO_LIMIT;
    if (!guest_readable(buffer, length)) return error(LINUX_EFAULT);
    struct jolt_description *description = description_at(index);
    int read_only = (description->flags & LINUX_O_ACCMODE) == LINUX_O_RDONLY;
    if (read_only && description->type != DESCRIPTION_TTY) return error(LINUX_EBADF);
    switch (description->type) {
    case DESCRIPTION_TTY:
        return tty_write(description->tty, (const char *)buffer, length);
    case DESCRIPTION_PTY_MASTER:
        return pty_master_write(description->tty, (const char *)buffer, length);
    case DESCRIPTION_PIPE_WRITE:
        return pipe_write(index, description->pipe, (const char *)buffer, length);
    case DESCRIPTION_STREAM:
        return pipe_write(index, description->output, (const char *)buffer, length);
    case DESCRIPTION_NULL:
    case DESCRIPTION_ZERO:
    case DESCRIPTION_RANDOM:
        return (LONG)length;
    case DESCRIPTION_FILE:
        return locked_file_write(index, (const void *)buffer, length, position);
    case DESCRIPTION_SOCKET:
        return network_write(index, (const char *)buffer, length);
    case DESCRIPTION_SERIAL:
        return serial_write(index, (const char *)buffer, length, (description->flags & LINUX_O_NONBLOCK) != 0);
    case DESCRIPTION_EVENT:
        return eventfd_write(index, (const char *)buffer, length);
    default:
        return error(LINUX_EBADF);
    }
}

static LONG transfer_vector(DWORD fd, DWORD vectors, DWORD count, int writing) {
    if (count > VECTOR_LIMIT) return error(LINUX_EINVAL);
    struct linux_iovec entries[VECTOR_LIMIT];
    if (!guest_copy_in(entries, vectors, count * sizeof entries[0])) return error(LINUX_EFAULT);
    LONG total = 0;
    for (DWORD index = 0; index < count; index++) {
        const struct linux_iovec *entry = &entries[index];
        if (!entry->length) continue;
        LONG result = writing ? write_descriptor(fd, entry->base, entry->length, -1) :
            read_descriptor(fd, entry->base, entry->length, -1);
        if (result < 0) return total ? total : result;
        total += result;
        if ((DWORD)result != entry->length) break;
    }
    return total;
}

LONG sys_read(const DWORD *arguments) {
    return read_descriptor(arguments[0], arguments[1], arguments[2], -1);
}

LONG sys_write(const DWORD *arguments) {
    return write_descriptor(arguments[0], arguments[1], arguments[2], -1);
}

LONG sys_pread64(const DWORD *arguments) {
    DWORD offset = arguments[4], offset_high = arguments[5];
    if ((LONG)offset < 0 || offset_high) return error(LINUX_EINVAL);
    return read_descriptor(arguments[0], arguments[1], arguments[2], (LONG)offset);
}

LONG sys_pwrite64(const DWORD *arguments) {
    DWORD offset = arguments[4], offset_high = arguments[5];
    if ((LONG)offset < 0 || offset_high) return error(LINUX_EINVAL);
    return write_descriptor(arguments[0], arguments[1], arguments[2], (LONG)offset);
}

LONG sys_readv(const DWORD *arguments) {
    return transfer_vector(arguments[0], arguments[1], arguments[2], 0);
}

LONG sys_writev(const DWORD *arguments) {
    return transfer_vector(arguments[0], arguments[1], arguments[2], 1);
}

static LONG open_at(DWORD directory, DWORD path, DWORD flags) {
    char absolute[JOLT_GUEST_PATH];
    LONG result = resolve_at(directory, path, absolute, NULL);
    if (result) return result;
    DWORD type = 0;
    result = jolt_file_open(absolute, flags, &type);
    if (result) {
        trace("open %s = %ld\r\n", absolute, result);
        return result;
    }
    DWORD stored = flags & ~(DWORD)(LINUX_O_CREAT | LINUX_O_EXCL | LINUX_O_TRUNC | LINUX_O_CLOEXEC);
    if (type == DESCRIPTION_TTY || type == DESCRIPTION_PTY_MASTER) stored = (stored & ~(DWORD)LINUX_O_ACCMODE) | LINUX_O_RDWR;
    jolt_lock();
    LONG terminal = JOLT_CONSOLE;
    if (type == DESCRIPTION_TTY || type == DESCRIPTION_PTY_MASTER) {
        result = tty_open(absolute, type, &terminal);
        if (result) {
            jolt_unlock();
            return result;
        }
    }
    if (type == DESCRIPTION_SERIAL) stored = (stored & ~(DWORD)LINUX_O_ACCMODE) | LINUX_O_RDWR;
    int index = jolt_description_alloc(type, stored, absolute);
    if (index >= 0 && type == DESCRIPTION_SERIAL) description_at(index)->pipe = (DWORD)serial_port_of(absolute);
    if (index >= 0) description_at(index)->tty = terminal;
    if (index < 0 && (type == DESCRIPTION_TTY || type == DESCRIPTION_PTY_MASTER)) jolt_tty_release_end(terminal, type == DESCRIPTION_PTY_MASTER);
    if (index < 0 && type == DESCRIPTION_SERIAL && --kernel()->serial[serial_port_of(absolute)].users <= 0) SetEvent(jolt.serial);
    int fd = index < 0 ? error(LINUX_ENFILE) : jolt_fd_install(self, 0, index, (flags & LINUX_O_CLOEXEC) != 0);
    if (index >= 0 && fd < 0) jolt_description_release(index);
    jolt_unlock();
    trace("open %s = %d\r\n", absolute, fd);
    return fd;
}

LONG sys_open(const DWORD *arguments) {
    return open_at(LINUX_AT_FDCWD, arguments[0], arguments[1]);
}

LONG sys_creat(const DWORD *arguments) {
    return open_at(LINUX_AT_FDCWD, arguments[0], LINUX_O_CREAT | LINUX_O_TRUNC | LINUX_O_WRONLY);
}

LONG sys_openat(const DWORD *arguments) {
    return open_at(arguments[0], arguments[1], arguments[2]);
}

void close_local(int fd) {
    if (fd < 0 || fd >= JOLT_FDS || !self->fds[fd]) return;
    int index = self->fds[fd] - 1;
    network_release(index);
    jolt_fd_close(self, fd);
    for (int other = 0; other < JOLT_FDS; other++)
        if (self->fds[other] == index + 1) return;
    jolt_files_drop(index);
}

LONG sys_close(const DWORD *arguments) {
    DWORD fd = arguments[0];
    if (fd_description(fd) < 0) return error(LINUX_EBADF);
    jolt_lock();
    close_local((int)fd);
    jolt_unlock();
    return 0;
}

static LONG duplicate(DWORD fd, DWORD target, int exact, int cloexec) {
    int index = fd_description(fd);
    if (index < 0 || target >= JOLT_FDS) return error(LINUX_EBADF);
    if (exact && fd == target) return (LONG)target;
    jolt_lock();
    if (exact) close_local((int)target);
    description_at(index)->references++;
    int result = jolt_fd_install(self, (int)target, index, cloexec);
    if (result < 0) jolt_description_release(index);
    jolt_unlock();
    return result;
}

LONG sys_dup(const DWORD *arguments) {
    return duplicate(arguments[0], 0, 0, 0);
}

LONG sys_dup2(const DWORD *arguments) {
    return duplicate(arguments[0], arguments[1], 1, 0);
}

LONG sys_dup3(const DWORD *arguments) {
    if (arguments[0] == arguments[1]) return error(LINUX_EINVAL);
    return duplicate(arguments[0], arguments[1], 1, (arguments[2] & LINUX_O_CLOEXEC) != 0);
}

static int free_pipe(void) {
    for (int index = 0; index < JOLT_PIPES; index++) {
        const struct jolt_pipe *pipe = &kernel()->pipes[index];
        if (pipe->readers <= 0 && pipe->writers <= 0) return index;
    }
    return -1;
}

static int claim_pipe(void) {
    int index = free_pipe();
    if (index < 0) return -1;
    struct jolt_pipe *pipe = &kernel()->pipes[index];
    pipe->read = pipe->written = 0;
    pipe->readers = pipe->writers = 1;
    return index;
}

static void release_pipe(int index) {
    if (index < 0) return;
    kernel()->pipes[index].readers = kernel()->pipes[index].writers = 0;
}

static LONG install_pair(DWORD fds, int first, int second, int cloexec) {
    int first_fd = jolt_fd_install(self, 0, first, cloexec);
    int second_fd = first_fd >= 0 ? jolt_fd_install(self, 0, second, cloexec) : error(LINUX_EMFILE);
    if (second_fd < 0) {
        if (first_fd >= 0) jolt_fd_close(self, first_fd);
        else jolt_description_release(first);
        jolt_description_release(second);
        return error(LINUX_EMFILE);
    }
    DWORD result[2] = { (DWORD)first_fd, (DWORD)second_fd };
    guest_copy_out(fds, result, sizeof result);
    return 0;
}

static int allocate_end(DWORD type, DWORD flags, const char *name, int input, int output) {
    int index = jolt_description_alloc(type, flags, name);
    if (index < 0) return -1;
    description_at(index)->pipe = (DWORD)input;
    description_at(index)->output = (DWORD)output;
    description_at(index)->shutdown = 0;
    return index;
}

static LONG create_pipe(DWORD fds, DWORD flags) {
    if (!guest_writable(fds, 2 * sizeof(DWORD))) return error(LINUX_EFAULT);
    if (flags & ~(DWORD)(LINUX_O_CLOEXEC | LINUX_O_NONBLOCK)) return error(LINUX_EINVAL);
    DWORD nonblocking = flags & LINUX_O_NONBLOCK;
    jolt_lock();
    int pipe = claim_pipe();
    int reader = pipe < 0 ? -1 : allocate_end(DESCRIPTION_PIPE_READ, LINUX_O_RDONLY | nonblocking, "pipe", pipe, pipe);
    int writer = reader < 0 ? -1 : allocate_end(DESCRIPTION_PIPE_WRITE, LINUX_O_WRONLY | nonblocking, "pipe", pipe, pipe);
    LONG result;
    if (writer < 0) {
        if (reader >= 0) jolt_description_release(reader);
        release_pipe(pipe);
        result = error(LINUX_ENFILE);
    } else {
        result = install_pair(fds, reader, writer, (flags & LINUX_O_CLOEXEC) != 0);
    }
    jolt_unlock();
    return result;
}

LONG sys_socketpair(const DWORD *arguments) {
    DWORD domain = arguments[0], type = arguments[1] & LINUX_SOCK_TYPE_MASK, flags = arguments[1] & ~LINUX_SOCK_TYPE_MASK;
    DWORD fds = arguments[3];
    if (domain != LINUX_AF_UNIX) return error(LINUX_EAFNOSUPPORT);
    if (type != LINUX_SOCK_STREAM) return error(LINUX_EOPNOTSUPP);
    if (!guest_writable(fds, 2 * sizeof(DWORD))) return error(LINUX_EFAULT);
    DWORD mode = LINUX_O_RDWR | (flags & LINUX_SOCK_NONBLOCK);
    jolt_lock();
    int forward = claim_pipe();
    int backward = forward < 0 ? -1 : claim_pipe();
    int first = backward < 0 ? -1 : allocate_end(DESCRIPTION_STREAM, mode, "socket", forward, backward);
    int second = first < 0 ? -1 : allocate_end(DESCRIPTION_STREAM, mode, "socket", backward, forward);
    LONG result;
    if (second < 0) {
        if (first >= 0) jolt_description_release(first);
        release_pipe(forward);
        release_pipe(backward);
        result = error(LINUX_ENFILE);
    } else {
        result = install_pair(fds, first, second, (flags & LINUX_SOCK_CLOEXEC) != 0);
    }
    jolt_unlock();
    return result;
}

LONG stream_shutdown(int index, DWORD how) {
    struct jolt_description *description = description_at(index);
    DWORD bits = 0;
    if (how == LINUX_SHUT_RD || how == LINUX_SHUT_RDWR) bits |= JOLT_SHUTDOWN_READ;
    if (how == LINUX_SHUT_WR || how == LINUX_SHUT_RDWR) bits |= JOLT_SHUTDOWN_WRITE;
    if (!bits) return error(LINUX_EINVAL);
    jolt_lock();
    DWORD fresh = bits & ~description->shutdown;
    if (fresh & JOLT_SHUTDOWN_READ) kernel()->pipes[description->pipe].readers--;
    if (fresh & JOLT_SHUTDOWN_WRITE) kernel()->pipes[description->output].writers--;
    description->shutdown |= fresh;
    jolt_wake_all();
    jolt_unlock();
    return 0;
}

LONG sys_pipe(const DWORD *arguments) {
    return create_pipe(arguments[0], 0);
}

LONG sys_pipe2(const DWORD *arguments) {
    return create_pipe(arguments[0], arguments[1]);
}

static void set_cloexec(DWORD fd, int enabled) {
    DWORD bit = 1UL << (fd % 32);
    jolt_lock();
    if (enabled) self->cloexec[fd / 32] |= bit;
    else self->cloexec[fd / 32] &= ~bit;
    jolt_unlock();
}

static void set_status_flags(struct jolt_description *description, DWORD flags) {
    DWORD changeable = LINUX_O_APPEND | LINUX_O_NONBLOCK;
    jolt_lock();
    description->flags = (description->flags & ~changeable) | (flags & changeable);
    jolt_unlock();
}

LONG sys_fcntl(const DWORD *arguments) {
    DWORD fd = arguments[0], command = arguments[1], argument = arguments[2];
    int index = fd_description(fd);
    if (index < 0) return error(LINUX_EBADF);
    struct jolt_description *description = description_at(index);
    switch (command) {
    case LINUX_F_DUPFD:
        return duplicate(fd, argument, 0, 0);
    case LINUX_F_DUPFD_CLOEXEC:
        return duplicate(fd, argument, 0, 1);
    case LINUX_F_GETFD:
        return (self->cloexec[fd / 32] >> (fd % 32)) & 1;
    case LINUX_F_SETFD:
        set_cloexec(fd, (argument & LINUX_FD_CLOEXEC) != 0);
        return 0;
    case LINUX_F_GETFL:
        return (LONG)((description->flags & ~JOLT_EVENTFD_SEMAPHORE) | LINUX_O_LARGEFILE);
    case LINUX_F_SETFL:
        set_status_flags(description, argument);
        return 0;
    case LINUX_F_GETLK: case LINUX_F_SETLK: case LINUX_F_SETLKW:
    case LINUX_F_GETLK64: case LINUX_F_SETLK64: case LINUX_F_SETLKW64:
    case LINUX_F_SETOWN: case LINUX_F_GETOWN:
        return 0;
    default:
        return error(LINUX_EINVAL);
    }
}

static int seekable_description(DWORD fd, LONG *failure) {
    int index = fd_description(fd);
    *failure = 0;
    if (index < 0) {
        *failure = error(LINUX_EBADF);
        return -1;
    }
    DWORD type = description_at(index)->type;
    if (type != DESCRIPTION_FILE && type != DESCRIPTION_DIRECTORY) *failure = error(LINUX_ESPIPE);
    return index;
}

static LONG locked_seek(int index, LONG offset, DWORD origin) {
    jolt_lock();
    LONG result = jolt_file_seek(index, offset, origin);
    jolt_unlock();
    return result;
}

LONG sys_lseek(const DWORD *arguments) {
    LONG failure;
    int index = seekable_description(arguments[0], &failure);
    if (failure) return failure;
    return locked_seek(index, (LONG)arguments[1], arguments[2]);
}

LONG sys_llseek(const DWORD *arguments) {
    DWORD offset_high = arguments[1], offset_low = arguments[2], result_pointer = arguments[3], origin = arguments[4];
    LONG failure;
    int index = seekable_description(arguments[0], &failure);
    if (failure) return failure;
    if (!guest_writable(result_pointer, sizeof(ULONGLONG))) return error(LINUX_EFAULT);
    if (offset_high && offset_high != 0xffffffffUL) return error(LINUX_EOVERFLOW);
    LONG position = locked_seek(index, (LONG)offset_low, origin);
    if (position < 0) return position;
    ULONGLONG wide = (DWORD)position;
    guest_copy_out(result_pointer, &wide, sizeof wide);
    return 0;
}

LONG sys_fsync(const DWORD *arguments) {
    int index = fd_description(arguments[0]);
    if (index < 0) return error(LINUX_EBADF);
    if (description_at(index)->type != DESCRIPTION_FILE) return 0;
    return jolt_file_sync(index);
}

static LONG truncate_descriptor(DWORD fd, DWORD length) {
    int index = fd_description(fd);
    if (index < 0) return error(LINUX_EBADF);
    const struct jolt_description *description = description_at(index);
    int writable = (description->flags & LINUX_O_ACCMODE) != LINUX_O_RDONLY;
    if (description->type != DESCRIPTION_FILE || !writable) return error(LINUX_EINVAL);
    return jolt_file_truncate(index, NULL, length);
}

LONG sys_ftruncate(const DWORD *arguments) {
    return truncate_descriptor(arguments[0], arguments[1]);
}

LONG sys_ftruncate64(const DWORD *arguments) {
    DWORD length_low = arguments[2], length_high = arguments[3];
    if (length_high) return error(LINUX_EFBIG);
    return truncate_descriptor(arguments[0], length_low);
}

LONG sys_fd_metadata(const DWORD *arguments) {
    return fd_description(arguments[0]) < 0 ? error(LINUX_EBADF) : 0;
}
