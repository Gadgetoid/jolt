#include <windows.h>
#include <string.h>
#include "worker.h"
#include "memory.h"
#include "process.h"
#include "syscalls.h"
#include "network.h"
#include "serial.h"

#define CHARACTER_WIDTH 8
#define CHARACTER_HEIGHT 16
#define VTIME_UNIT_MILLISECONDS 100

static DWORD take_input(char *buffer, DWORD length, int canonical) {
    struct jolt_kernel *k = kernel();
    DWORD consumed = (DWORD)k->input_read;
    DWORD available = (DWORD)k->input_written - consumed;
    DWORD count = 0;
    while (count < length && count < available) {
        char character = k->input[(consumed + count) % JOLT_INPUT_SIZE];
        buffer[count++] = character;
        if (canonical && character == '\n') break;
    }
    InterlockedExchange(&k->input_read, (LONG)(consumed + count));
    return count;
}

static LONG background_read(void) {
    DWORD bit = LINUX_SIGNAL_BIT(LINUX_SIGTTIN);
    if ((self->ignored & bit) || (state.blocked & bit)) return error(LINUX_EIO);
    jolt_signal_group(self->pgid, LINUX_SIGTTIN);
    return error(LINUX_EINTR);
}

LONG tty_read(char *buffer, DWORD length, int nonblocking) {
    struct jolt_kernel *k = kernel();
    DWORD began = GetTickCount();
    for (;;) {
        jolt_lock();
        if (self->pgid != k->foreground) {
            LONG result = background_read();
            jolt_unlock();
            return result;
        }
        const struct linux_termios *settings = &k->termios;
        int canonical = (settings->local_flags & LINUX_ICANON) != 0;
        BYTE minimum = settings->control[LINUX_VMIN], time = settings->control[LINUX_VTIME];
        if ((DWORD)k->input_written != (DWORD)k->input_read) {
            DWORD count = take_input(buffer, length, canonical);
            self->waiting = WAIT_NONE;
            jolt_unlock();
            return (LONG)count;
        }
        if (canonical && k->input_eof > 0) {
            k->input_eof--;
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
        jolt_unlock();
        block_until(timeout);
    }
}

static void discard_input(void) {
    jolt_lock();
    kernel()->input_read = kernel()->input_written;
    kernel()->input_eof = 0;
    jolt_unlock();
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
        count = (DWORD)k->input_written - (DWORD)k->input_read;
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

static LONG get_settings(DWORD pointer) {
    return guest_copy_out(pointer, &kernel()->termios, sizeof(struct linux_termios)) ? 0 : error(LINUX_EFAULT);
}

static LONG set_settings(DWORD request, DWORD pointer) {
    struct linux_termios settings;
    if (!guest_copy_in(&settings, pointer, sizeof settings)) return error(LINUX_EFAULT);
    jolt_lock();
    kernel()->termios = settings;
    jolt_unlock();
    if (request == LINUX_TCSETSF) discard_input();
    return 0;
}

static LONG get_window_size(DWORD pointer) {
    struct linux_winsize size;
    size.rows = kernel()->rows;
    size.columns = kernel()->columns;
    size.x_pixels = (WORD)(size.columns * CHARACTER_WIDTH);
    size.y_pixels = (WORD)(size.rows * CHARACTER_HEIGHT);
    return guest_copy_out(pointer, &size, sizeof size) ? 0 : error(LINUX_EFAULT);
}

static LONG put_process_id(DWORD pointer, LONG value) {
    return guest_copy_out(pointer, &value, sizeof value) ? 0 : error(LINUX_EFAULT);
}

static LONG set_foreground(DWORD pointer) {
    LONG group;
    if (!guest_copy_in(&group, pointer, sizeof group)) return error(LINUX_EFAULT);
    jolt_lock();
    kernel()->foreground = group;
    jolt_unlock();
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

static LONG terminal_ioctl(DWORD request, DWORD argument) {
    switch (request) {
    case LINUX_TCGETS:
        return get_settings(argument);
    case LINUX_TCSETS: case LINUX_TCSETSW: case LINUX_TCSETSF:
        return set_settings(request, argument);
    case LINUX_TIOCGWINSZ:
        return get_window_size(argument);
    case LINUX_TIOCGPGRP:
        return put_process_id(argument, kernel()->foreground);
    case LINUX_TIOCGSID:
        return put_process_id(argument, self->sid);
    case LINUX_TIOCSPGRP:
        return set_foreground(argument);
    case LINUX_TCFLSH:
        if (argument == LINUX_TCIFLUSH || argument == LINUX_TCIOFLUSH) discard_input();
        return 0;
    case LINUX_KDGKBTYPE:
        return get_keyboard_type(argument);
    case LINUX_KIOCSOUND:
        return play_tone(argument, 0);
    case LINUX_KDMKTONE:
        return play_tone(HIWORD(argument) ? LOWORD(argument) : 0, HIWORD(argument));
    case LINUX_TIOCSWINSZ: case LINUX_TIOCSCTTY: case LINUX_TIOCNOTTY: case LINUX_TCXONC: case LINUX_TCSBRK:
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
    if (description->type != DESCRIPTION_TTY) return error(LINUX_ENOTTY);
    return terminal_ioctl(request, argument);
}
