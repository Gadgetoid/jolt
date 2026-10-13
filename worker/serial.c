#include <windows.h>
#include <string.h>
#include "serial.h"
#include "worker.h"
#include "memory.h"

#define VTIME_UNIT_MILLISECONDS 100
#define DEVICE_PREFIX "/dev/"
#define SERIAL_NAME "ttyS"
#define DEFAULT_LINES (LINUX_TIOCM_DTR | LINUX_TIOCM_RTS)

static struct jolt_serial *port_state(int port) {
    return &kernel()->serial[port];
}

int serial_port_named(const char *name) {
    size_t length = strlen(SERIAL_NAME);
    if (strncmp(name, SERIAL_NAME, length)) return -1;
    char digit = name[length];
    if (digit < '0' || digit >= '0' + JOLT_SERIAL_PORTS || name[length + 1]) return -1;
    return digit - '0';
}

int serial_port_of(const char *absolute) {
    size_t length = strlen(DEVICE_PREFIX);
    return strncmp(absolute, DEVICE_PREFIX, length) ? -1 : serial_port_named(absolute + length);
}

static void default_settings(struct jolt_serial *serial) {
    memset(&serial->termios, 0, sizeof serial->termios);
    serial->termios.control_flags = LINUX_B9600 | LINUX_CS8 | LINUX_CREAD | LINUX_CLOCAL | LINUX_HUPCL;
    serial->termios.control[LINUX_VMIN] = 1;
    serial->modem_lines = DEFAULT_LINES;
    serial->break_state = 0;
}

static LONG open_error(DWORD code) {
    switch (code) {
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: return error(LINUX_EBUSY);
    case ERROR_FILE_NOT_FOUND: case ERROR_DEV_NOT_EXIST: return error(LINUX_ENODEV);
    default: return error(LINUX_EIO);
    }
}

LONG serial_open(int port) {
    struct jolt_serial *serial = port_state(port);
    jolt_lock();
    if (serial->users++ == 0) {
        if (!serial->termios.control_flags) default_settings(serial);
        serial->state = SERIAL_OPENING;
    }
    jolt_unlock();
    SetEvent(jolt.serial);
    for (;;) {
        jolt_lock();
        LONG current = serial->state;
        if (current == SERIAL_OPEN) {
            jolt_unlock();
            return 0;
        }
        if (current == SERIAL_FAILED) {
            DWORD code = serial->error;
            if (--serial->users <= 0) SetEvent(jolt.serial);
            jolt_unlock();
            return open_error(code);
        }
        jolt_unlock();
        block_until(INFINITE);
    }
}

static int port_for(int index) {
    return (int)description_at(index)->pipe;
}

static DWORD read_timeout(const struct jolt_serial *serial) {
    BYTE minimum = serial->termios.control[LINUX_VMIN], time = serial->termios.control[LINUX_VTIME];
    if (minimum || !time) return INFINITE;
    return time * VTIME_UNIT_MILLISECONDS;
}

LONG serial_read(int index, char *buffer, DWORD length, int nonblocking) {
    struct jolt_serial *serial = port_state(port_for(index));
    DWORD began = GetTickCount(), timeout = read_timeout(serial);
    for (;;) {
        jolt_lock();
        DWORD read = (DWORD)serial->received_read, available = (DWORD)serial->received_written - read;
        LONG result = 1;
        if (available) {
            DWORD count = available < length ? available : length;
            for (DWORD offset = 0; offset < count; offset++)
                buffer[offset] = serial->received[(read + offset) % JOLT_SERIAL_BUFFER];
            InterlockedExchange((LPLONG)&serial->received_read, (LONG)(read + count));
            result = (LONG)count;
        } else if (serial->state != SERIAL_OPEN) {
            result = error(LINUX_EIO);
        } else if (nonblocking) {
            result = error(LINUX_EAGAIN);
        } else if (signal_ready()) {
            result = error(LINUX_EINTR);
        } else if (!serial->termios.control[LINUX_VMIN] && !remaining(began, timeout)) {
            result = 0;
        }
        if (result <= 0 || available) {
            self->waiting = WAIT_NONE;
            jolt_unlock();
            if (available) SetEvent(jolt.serial);
            return result;
        }
        self->waiting = WAIT_PIPE;
        jolt_unlock();
        block_until(remaining(began, timeout));
    }
}

LONG serial_write(int index, const char *buffer, DWORD length, int nonblocking) {
    struct jolt_serial *serial = port_state(port_for(index));
    DWORD done = 0;
    for (;;) {
        jolt_lock();
        DWORD written = (DWORD)serial->sent_written;
        DWORD space = JOLT_SERIAL_BUFFER - (written - (DWORD)serial->sent_read);
        DWORD count = length - done < space ? length - done : space;
        for (DWORD offset = 0; offset < count; offset++)
            serial->sent[(written + offset) % JOLT_SERIAL_BUFFER] = buffer[done + offset];
        InterlockedExchange((LPLONG)&serial->sent_written, (LONG)(written + count));
        done += count;
        LONG stop = 0;
        if (serial->state != SERIAL_OPEN) stop = done ? (LONG)done : error(LINUX_EIO);
        else if (done == length) stop = (LONG)done;
        else if (nonblocking) stop = done ? (LONG)done : error(LINUX_EAGAIN);
        else if (signal_ready()) stop = done ? (LONG)done : error(LINUX_EINTR);
        if (count) SetEvent(jolt.serial);
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

int serial_readiness(int index) {
    const struct jolt_serial *serial = port_state(port_for(index));
    int ready = 0;
    if (serial->received_written != serial->received_read) ready |= LINUX_POLLIN;
    if ((DWORD)serial->sent_written - (DWORD)serial->sent_read < JOLT_SERIAL_BUFFER) ready |= LINUX_POLLOUT;
    if (serial->state != SERIAL_OPEN) ready |= LINUX_POLLERR | LINUX_POLLHUP;
    return ready;
}

DWORD serial_pending(int index) {
    const struct jolt_serial *serial = port_state(port_for(index));
    return (DWORD)serial->received_written - (DWORD)serial->received_read;
}

static void request_purge(struct jolt_serial *serial, DWORD selector) {
    LONG purge = 0;
    if (selector == LINUX_TCIFLUSH || selector == LINUX_TCIOFLUSH) purge |= PURGE_RXCLEAR;
    if (selector == LINUX_TCOFLUSH || selector == LINUX_TCIOFLUSH) purge |= PURGE_TXCLEAR;
    jolt_lock();
    serial->purge |= purge;
    jolt_unlock();
    SetEvent(jolt.serial);
}

static LONG set_settings(struct jolt_serial *serial, DWORD request, DWORD pointer) {
    struct linux_termios settings;
    if (!guest_copy_in(&settings, pointer, sizeof settings)) return error(LINUX_EFAULT);
    jolt_lock();
    serial->termios = settings;
    serial->settings_changed = 1;
    jolt_unlock();
    if (request == LINUX_TCSETSF) request_purge(serial, LINUX_TCIFLUSH);
    SetEvent(jolt.serial);
    return 0;
}

static LONG change_lines(struct jolt_serial *serial, DWORD request, DWORD pointer) {
    DWORD lines;
    if (!guest_copy_in(&lines, pointer, sizeof lines)) return error(LINUX_EFAULT);
    lines &= LINUX_TIOCM_DTR | LINUX_TIOCM_RTS;
    jolt_lock();
    if (request == LINUX_TIOCMSET) serial->modem_lines = lines;
    else if (request == LINUX_TIOCMBIS) serial->modem_lines |= lines;
    else serial->modem_lines &= ~lines;
    serial->modem_changed = 1;
    jolt_unlock();
    SetEvent(jolt.serial);
    return 0;
}

static LONG put_value(DWORD pointer, DWORD value) {
    return guest_copy_out(pointer, &value, sizeof value) ? 0 : error(LINUX_EFAULT);
}

static LONG set_break(struct jolt_serial *serial, LONG state) {
    InterlockedExchange(&serial->break_state, state);
    SetEvent(jolt.serial);
    return 0;
}

LONG serial_ioctl(int index, DWORD request, DWORD argument) {
    struct jolt_serial *serial = port_state(port_for(index));
    switch (request) {
    case LINUX_TCGETS:
        return guest_copy_out(argument, &serial->termios, sizeof serial->termios) ? 0 : error(LINUX_EFAULT);
    case LINUX_TCSETS: case LINUX_TCSETSW: case LINUX_TCSETSF:
        return set_settings(serial, request, argument);
    case LINUX_TCFLSH:
        request_purge(serial, argument);
        return 0;
    case LINUX_TIOCMGET:
        return put_value(argument, serial->modem_lines | serial->modem_status);
    case LINUX_TIOCMSET: case LINUX_TIOCMBIS: case LINUX_TIOCMBIC:
        return change_lines(serial, request, argument);
    case LINUX_TIOCOUTQ:
        return put_value(argument, (DWORD)serial->sent_written - (DWORD)serial->sent_read);
    case LINUX_TIOCSBRK:
        return set_break(serial, 1);
    case LINUX_TIOCCBRK:
        return set_break(serial, 0);
    case LINUX_TIOCEXCL: case LINUX_TIOCNXCL: case LINUX_TCXONC: case LINUX_TCSBRK: case LINUX_TIOCSCTTY:
        return 0;
    default:
        return error(LINUX_ENOTTY);
    }
}
