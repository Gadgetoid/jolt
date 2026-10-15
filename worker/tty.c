#include <windows.h>
#include <string.h>
#include "worker.h"
#include "memory.h"
#include "process.h"
#include "syscalls.h"
#include "network.h"
#include "serial.h"

#define VTIME_UNIT_MILLISECONDS 100

static struct jolt_tty *tty_at(int index) {
    return &kernel()->ttys[index];
}

static DWORD take_input(struct jolt_tty *tty, char *buffer, DWORD length, int canonical) {
    DWORD consumed = (DWORD)tty->input_read;
    DWORD available = (DWORD)tty->input_written - consumed;
    DWORD count = 0;
    while (count < length && count < available) {
        char character = tty->input[(consumed + count) % JOLT_INPUT_SIZE];
        buffer[count++] = character;
        if (canonical && character == '\n') break;
    }
    InterlockedExchange(&tty->input_read, (LONG)(consumed + count));
    return count;
}

static LONG background_read(void) {
    DWORD bit = LINUX_SIGNAL_BIT(LINUX_SIGTTIN);
    if ((self->ignored & bit) || (state.blocked & bit)) return error(LINUX_EIO);
    jolt_signal_group(self->pgid, LINUX_SIGTTIN);
    return error(LINUX_EINTR);
}

LONG tty_read(int index, char *buffer, DWORD length, int nonblocking) {
    struct jolt_tty *tty = tty_at(index);
    DWORD began = GetTickCount();
    for (;;) {
        jolt_lock();
        if (self->ctty == index && self->pgid != tty->foreground) {
            LONG result = background_read();
            jolt_unlock();
            return result;
        }
        const struct linux_termios *settings = &tty->termios;
        int canonical = (settings->local_flags & LINUX_ICANON) != 0;
        BYTE minimum = settings->control[LINUX_VMIN], time = settings->control[LINUX_VTIME];
        if ((DWORD)tty->input_written != (DWORD)tty->input_read) {
            DWORD count = take_input(tty, buffer, length, canonical);
            self->waiting = WAIT_NONE;
            jolt_unlock();
            return (LONG)count;
        }
        if ((canonical && tty->input_eof > 0) || tty->hung_up) {
            if (!tty->hung_up) tty->input_eof--;
            jolt_unlock();
            return 0;
        }
        DWORD timeout = INFINITE;
        if (!canonical && !minimum) timeout = time ? remaining(began, time * VTIME_UNIT_MILLISECONDS) : 0;
        LONG result = 1;
        if (!timeout) result = 0;
        else if (nonblocking) result = error(LINUX_EAGAIN);
        else if (signal_ready()) result = error(LINUX_EINTR);
        if (result <= 0) {
            self->waiting = WAIT_NONE;
            jolt_unlock();
            return result;
        }
        self->waiting = WAIT_TTY;
        self->wait_object = JOLT_WAIT_TTY_INPUT(index);
        jolt_unlock();
        block_until(timeout);
    }
}

LONG tty_write(int index, const char *buffer, DWORD length) {
    if (tty_at(index)->hung_up) return error(LINUX_EIO);
    DWORD written = jolt_tty_write(index, buffer, length, 1);
    return written || !length ? (LONG)written : error(LINUX_EIO);
}

static int slave_gone(const struct jolt_tty *tty) {
    return tty->opened && tty->slaves <= 0;
}

LONG pty_master_read(int index, char *buffer, DWORD length, int nonblocking) {
    struct jolt_tty *tty = tty_at(index);
    for (;;) {
        jolt_lock();
        DWORD consumed = (DWORD)tty->output_read;
        DWORD available = (DWORD)tty->output_written - consumed;
        if (available) {
            DWORD count = available < length ? available : length;
            for (DWORD position = 0; position < count; position++)
                buffer[position] = tty->output[(consumed + position) % JOLT_TTY_SIZE];
            InterlockedExchange(&tty->output_read, (LONG)(consumed + count));
            self->waiting = WAIT_NONE;
            jolt_wake_object(JOLT_WAIT_TTY_SPACE(index));
            jolt_unlock();
            return (LONG)count;
        }
        LONG result = 1;
        if (slave_gone(tty)) result = error(LINUX_EIO);
        else if (nonblocking) result = error(LINUX_EAGAIN);
        else if (signal_ready()) result = error(LINUX_EINTR);
        if (result <= 0) {
            self->waiting = WAIT_NONE;
            jolt_unlock();
            return result;
        }
        self->waiting = WAIT_PIPE;
        self->wait_object = JOLT_WAIT_TTY_OUTPUT(index);
        jolt_unlock();
        block_until(INFINITE);
    }
}

LONG pty_master_write(int index, const char *buffer, DWORD length) {
    return (LONG)jolt_tty_master_write(index, buffer, length);
}

int tty_readiness(int index, int master) {
    const struct jolt_tty *tty = tty_at(index);
    int ready = LINUX_POLLOUT;
    if (master) {
        if ((DWORD)tty->output_written != (DWORD)tty->output_read) ready |= LINUX_POLLIN;
        if (slave_gone(tty)) ready |= LINUX_POLLIN | LINUX_POLLHUP;
        return ready;
    }
    if ((DWORD)tty->input_written != (DWORD)tty->input_read || tty->input_eof) ready |= LINUX_POLLIN;
    if (tty->hung_up) ready = LINUX_POLLIN | LINUX_POLLHUP;
    return ready;
}

LONG tty_open(const char *absolute, DWORD type, LONG *index) {
    if (type == DESCRIPTION_PTY_MASTER) {
        *index = jolt_tty_open();
        return *index == JOLT_NO_TTY ? error(LINUX_ENOSPC) : 0;
    }
    if (!strcmp(absolute, "/dev/console")) {
        *index = JOLT_CONSOLE;
        return 0;
    }
    if (!strncmp(absolute, "/dev/pts/", strlen("/dev/pts/"))) {
        LONG number = atol(absolute + strlen("/dev/pts/"));
        *index = number + 1;
        if (*index <= JOLT_CONSOLE || *index >= JOLT_TTYS) return error(LINUX_ENOENT);
        struct jolt_tty *tty = tty_at(*index);
        if (tty->state != TTY_PTY || tty->hung_up) return error(LINUX_EIO);
        if (tty->locked) return error(LINUX_EIO);
        tty->slaves++;
        tty->opened = 1;
        return 0;
    }
    if (self->ctty == JOLT_NO_TTY) return error(LINUX_ENXIO);
    *index = self->ctty;
    if (*index != JOLT_CONSOLE) tty_at(*index)->slaves++;
    return 0;
}

int tty_number(int index) {
    return index - 1;
}

static LONG set_nonblocking(struct jolt_description *description, DWORD pointer) {
    DWORD enabled;
    if (!guest_copy_in(&enabled, pointer, sizeof enabled)) return error(LINUX_EFAULT);
    jolt_lock();
    if (enabled) description->flags |= LINUX_O_NONBLOCK;
    else description->flags &= ~(DWORD)LINUX_O_NONBLOCK;
    jolt_unlock();
    return 0;
}

static LONG bytes_available(const struct jolt_description *description, DWORD pointer) {
    struct jolt_kernel *k = kernel();
    DWORD count = 0;
    if (description->type == DESCRIPTION_TTY) {
        const struct jolt_tty *tty = tty_at(description->tty);
        count = (DWORD)tty->input_written - (DWORD)tty->input_read;
    } else if (description->type == DESCRIPTION_PTY_MASTER) {
        const struct jolt_tty *tty = tty_at(description->tty);
        count = (DWORD)tty->output_written - (DWORD)tty->output_read;
    } else if (description->type == DESCRIPTION_PIPE_READ || description->type == DESCRIPTION_STREAM) {
        const struct jolt_pipe *pipe = &k->pipes[description->pipe];
        count = pipe->written - pipe->read;
    } else if (description->type == DESCRIPTION_SOCKET) {
        count = network_pending((int)(description - k->descriptions));
    } else if (description->type == DESCRIPTION_SERIAL) {
        count = serial_pending((int)(description - k->descriptions));
    }
    return guest_copy_out(pointer, &count, sizeof count) ? 0 : error(LINUX_EFAULT);
}

static LONG get_settings(struct jolt_tty *tty, DWORD pointer) {
    return guest_copy_out(pointer, &tty->termios, sizeof(struct linux_termios)) ? 0 : error(LINUX_EFAULT);
}

static LONG set_settings(int index, DWORD request, DWORD pointer) {
    struct linux_termios settings;
    if (!guest_copy_in(&settings, pointer, sizeof settings)) return error(LINUX_EFAULT);
    jolt_lock();
    tty_at(index)->termios = settings;
    jolt_unlock();
    if (request == LINUX_TCSETSF) jolt_tty_discard_input(index);
    return 0;
}

static LONG get_window_size(const struct jolt_tty *tty, DWORD pointer) {
    struct linux_winsize size;
    size.rows = tty->rows;
    size.columns = tty->columns;
    size.x_pixels = tty->width;
    size.y_pixels = tty->height;
    return guest_copy_out(pointer, &size, sizeof size) ? 0 : error(LINUX_EFAULT);
}

static LONG set_window_size(int index, DWORD pointer) {
    struct linux_winsize size;
    if (!guest_copy_in(&size, pointer, sizeof size)) return error(LINUX_EFAULT);
    if (index != JOLT_CONSOLE) jolt_tty_resize(index, size.rows, size.columns, size.x_pixels, size.y_pixels);
    return 0;
}

static LONG put_value(DWORD pointer, LONG value) {
    return guest_copy_out(pointer, &value, sizeof value) ? 0 : error(LINUX_EFAULT);
}

static LONG set_foreground(struct jolt_tty *tty, DWORD pointer) {
    LONG group;
    if (!guest_copy_in(&group, pointer, sizeof group)) return error(LINUX_EFAULT);
    jolt_lock();
    tty->foreground = group;
    jolt_unlock();
    return 0;
}

static LONG take_controlling(int index) {
    struct jolt_tty *tty = tty_at(index);
    jolt_lock();
    LONG result = 0;
    if (self->ctty == index) result = 0;
    else if (self->sid != self->pid || self->ctty != JOLT_NO_TTY) result = error(LINUX_EPERM);
    else {
        self->ctty = index;
        tty->session = self->sid;
        tty->foreground = self->pgid;
    }
    jolt_unlock();
    return result;
}

static LONG drop_controlling(int index) {
    if (self->ctty != index) return error(LINUX_ENOTTY);
    self->ctty = JOLT_NO_TTY;
    return 0;
}

static LONG set_pty_lock(int index, DWORD pointer) {
    LONG locked;
    if (!guest_copy_in(&locked, pointer, sizeof locked)) return error(LINUX_EFAULT);
    tty_at(index)->locked = locked != 0;
    return 0;
}

static LONG get_keyboard_type(DWORD pointer) {
    BYTE type = LINUX_KB_101;
    return guest_copy_out(pointer, &type, sizeof type) ? 0 : error(LINUX_EFAULT);
}

static LONG play_tone(DWORD divisor, DWORD milliseconds) {
    DWORD frequency = divisor ? LINUX_TONE_CLOCK / divisor : 0;
    HWND window = (HWND)kernel()->window;
    if (window) PostMessageW(window, JOLT_SOUND_MESSAGE, frequency, MAKELPARAM(milliseconds, slot));
    return 0;
}

static LONG terminal_ioctl(int index, DWORD request, DWORD argument) {
    struct jolt_tty *tty = tty_at(index);
    switch (request) {
    case LINUX_TCGETS:
        return get_settings(tty, argument);
    case LINUX_TCSETS: case LINUX_TCSETSW: case LINUX_TCSETSF:
        return set_settings(index, request, argument);
    case LINUX_TIOCGWINSZ:
        return get_window_size(tty, argument);
    case LINUX_TIOCSWINSZ:
        return set_window_size(index, argument);
    case LINUX_TIOCGPGRP:
        return put_value(argument, tty->foreground);
    case LINUX_TIOCGSID:
        return put_value(argument, tty->session ? tty->session : self->sid);
    case LINUX_TIOCSPGRP:
        return set_foreground(tty, argument);
    case LINUX_TIOCSCTTY:
        return take_controlling(index);
    case LINUX_TIOCNOTTY:
        return drop_controlling(index);
    case LINUX_TCFLSH:
        if (argument == LINUX_TCIFLUSH || argument == LINUX_TCIOFLUSH) jolt_tty_discard_input(index);
        return 0;
    case LINUX_KDGKBTYPE:
        return get_keyboard_type(argument);
    case LINUX_KIOCSOUND:
        return play_tone(argument, 0);
    case LINUX_KDMKTONE:
        return play_tone(HIWORD(argument) ? LOWORD(argument) : 0, HIWORD(argument));
    case LINUX_TCXONC: case LINUX_TCSBRK:
        return 0;
    default:
        return error(LINUX_EINVAL);
    }
}

LONG sys_ioctl(const DWORD *arguments) {
    DWORD fd = arguments[0], request = arguments[1], argument = arguments[2];
    int index = fd_description(fd);
    if (index < 0) return error(LINUX_EBADF);
    struct jolt_description *description = description_at(index);
    if (request == LINUX_FIONBIO) return set_nonblocking(description, argument);
    if (request == LINUX_FIONREAD) return bytes_available(description, argument);
    if (description->type == DESCRIPTION_SERIAL) return serial_ioctl(index, request, argument);
    int master = description->type == DESCRIPTION_PTY_MASTER;
    if (!master && description->type != DESCRIPTION_TTY) return error(LINUX_ENOTTY);
    if (request == LINUX_TIOCGPTN) return master ? put_value(argument, tty_number(description->tty)) : error(LINUX_ENOTTY);
    if (request == LINUX_TIOCSPTLCK) return master ? set_pty_lock(description->tty, argument) : error(LINUX_ENOTTY);
    return terminal_ioctl(description->tty, request, argument);
}
