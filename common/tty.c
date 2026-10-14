#include <windows.h>
#include <string.h>
#include "kernel.h"

#define CONTROL(letter) ((letter) & 0x1f)
#define ASCII_DELETE 0x7f
#define OUTPUT_RETRY_MILLISECONDS 5

static struct jolt_tty *tty_at(int index) {
    return &jolt.kernel->ttys[index];
}

void jolt_tty_default_settings(struct linux_termios *settings) {
    memset(settings, 0, sizeof *settings);
    settings->input_flags = LINUX_ICRNL;
    settings->output_flags = LINUX_OPOST | LINUX_ONLCR;
    settings->control_flags = LINUX_B38400 | LINUX_CS8 | LINUX_CREAD;
    settings->local_flags = LINUX_ISIG | LINUX_ICANON | LINUX_ECHO | LINUX_ECHOE | LINUX_ECHOK |
        LINUX_ECHOCTL | LINUX_ECHOKE | LINUX_IEXTEN;
    settings->control[LINUX_VINTR] = CONTROL('C');
    settings->control[LINUX_VQUIT] = CONTROL('\\');
    settings->control[LINUX_VERASE] = ASCII_DELETE;
    settings->control[LINUX_VKILL] = CONTROL('U');
    settings->control[LINUX_VEOF] = CONTROL('D');
    settings->control[LINUX_VMIN] = 1;
    settings->control[LINUX_VSTART] = CONTROL('Q');
    settings->control[LINUX_VSTOP] = CONTROL('S');
    settings->control[LINUX_VSUSP] = CONTROL('Z');
    settings->control[LINUX_VREPRINT] = CONTROL('R');
    settings->control[LINUX_VDISCARD] = CONTROL('O');
    settings->control[LINUX_VWERASE] = CONTROL('W');
    settings->control[LINUX_VLNEXT] = CONTROL('V');
}

int jolt_tty_open(void) {
    const struct jolt_tty *console = tty_at(JOLT_CONSOLE);
    for (int index = JOLT_CONSOLE + 1; index < JOLT_TTYS; index++) {
        struct jolt_tty *tty = tty_at(index);
        if (tty->state != TTY_FREE) continue;
        memset(tty, 0, sizeof *tty);
        tty->state = TTY_PTY;
        tty->masters = 1;
        tty->locked = 1;
        tty->rows = console->rows;
        tty->columns = console->columns;
        jolt_tty_default_settings(&tty->termios);
        return index;
    }
    return JOLT_NO_TTY;
}

static void notify_output(int index) {
    struct jolt_kernel *kernel = jolt.kernel;
    if (index != JOLT_CONSOLE) {
        jolt_wake_all();
        return;
    }
    if (kernel->window && !InterlockedExchange(&tty_at(index)->output_notified, 1))
        PostMessageW((HWND)kernel->window, JOLT_TTY_MESSAGE, 0, 0);
}

DWORD jolt_tty_write(int index, const char *bytes, DWORD length, int block) {
    struct jolt_tty *tty = tty_at(index);
    DWORD done = 0;
    while (done < length) {
        jolt_lock();
        if (tty->hung_up) {
            jolt_unlock();
            break;
        }
        DWORD output = tty->termios.output_flags;
        int translate = (output & LINUX_OPOST) && (output & LINUX_ONLCR);
        DWORD written = (DWORD)tty->output_written;
        DWORD space = JOLT_TTY_SIZE - (written - (DWORD)tty->output_read);
        while (done < length) {
            char character = bytes[done];
            DWORD needed = character == '\n' && translate ? 2 : 1;
            if (space < needed) break;
            if (needed == 2) tty->output[written++ % JOLT_TTY_SIZE] = '\r';
            tty->output[written++ % JOLT_TTY_SIZE] = character;
            space -= needed;
            done++;
        }
        InterlockedExchange(&tty->output_written, (LONG)written);
        notify_output(index);
        jolt_unlock();
        if (done < length) {
            if (!block) break;
            Sleep(OUTPUT_RETRY_MILLISECONDS);
        }
    }
    return done;
}

static int is_control(BYTE value) {
    return value < 32 || value == ASCII_DELETE;
}

static void echo(int index, const char *bytes, DWORD length) {
    jolt_tty_write(index, bytes, length, 0);
}

static void echo_character(int index, char character) {
    DWORD local = tty_at(index)->termios.local_flags;
    if (!(local & LINUX_ECHO)) return;
    BYTE value = (BYTE)character;
    if ((local & LINUX_ECHOCTL) && is_control(value) && character != '\n' && character != '\t') {
        char caret[2] = { '^', (char)(value == ASCII_DELETE ? '?' : value + '@') };
        echo(index, caret, sizeof caret);
    } else {
        echo(index, &character, 1);
    }
}

static void push_input(int index, const char *bytes, DWORD length) {
    struct jolt_tty *tty = tty_at(index);
    DWORD written = (DWORD)tty->input_written;
    for (DWORD position = 0; position < length; position++) {
        if (written - (DWORD)tty->input_read >= JOLT_INPUT_SIZE) break;
        tty->input[written++ % JOLT_INPUT_SIZE] = bytes[position];
    }
    InterlockedExchange(&tty->input_written, (LONG)written);
    jolt_wake_all();
}

void jolt_tty_discard_input(int index) {
    jolt_lock();
    struct jolt_tty *tty = tty_at(index);
    tty->input_read = tty->input_written;
    tty->input_eof = 0;
    jolt_unlock();
}

static void erase_characters(int index, DWORD count) {
    struct jolt_tty *tty = tty_at(index);
    DWORD local = tty->termios.local_flags;
    int visible = (local & LINUX_ECHO) && (local & LINUX_ECHOE);
    while (count-- && tty->line_length) {
        BYTE value = (BYTE)tty->line[--tty->line_length];
        if (!visible) continue;
        int width = (local & LINUX_ECHOCTL) && is_control(value) && value != '\t' ? 2 : 1;
        while (width--) echo(index, "\b \b", 3);
    }
}

static void erase_word(int index) {
    struct jolt_tty *tty = tty_at(index);
    while (tty->line_length && tty->line[tty->line_length - 1] == ' ') erase_characters(index, 1);
    while (tty->line_length && tty->line[tty->line_length - 1] != ' ') erase_characters(index, 1);
}

static char translate_input(const struct linux_termios *settings, char character) {
    DWORD input = settings->input_flags;
    if (input & LINUX_ISTRIP) character &= 0x7f;
    if (character == '\r' && (input & LINUX_ICRNL)) return '\n';
    if (character == '\n' && (input & LINUX_INLCR)) return '\r';
    return character;
}

static int signal_for(const struct linux_termios *settings, BYTE value) {
    const BYTE *control = settings->control;
    if (!value) return 0;
    if (value == control[LINUX_VINTR]) return LINUX_SIGINT;
    if (value == control[LINUX_VQUIT]) return LINUX_SIGQUIT;
    if (value == control[LINUX_VSUSP]) return LINUX_SIGTSTP;
    return 0;
}

static void raise_signal(int index, char character, int signal) {
    struct jolt_tty *tty = tty_at(index);
    tty->line_length = 0;
    if (signal != LINUX_SIGTSTP) {
        tty->input_read = tty->input_written;
        tty->input_eof = 0;
    }
    echo_character(index, character);
    jolt_signal_group(tty->foreground, signal);
}

static void canonical_input(int index, char character) {
    struct jolt_tty *tty = tty_at(index);
    const BYTE *control = tty->termios.control;
    BYTE value = (BYTE)character;
    if (value == control[LINUX_VERASE] || value == '\b') {
        erase_characters(index, 1);
        return;
    }
    if (value == control[LINUX_VKILL]) {
        erase_characters(index, tty->line_length);
        return;
    }
    if (value == control[LINUX_VWERASE]) {
        erase_word(index);
        return;
    }
    if (value == control[LINUX_VEOF]) {
        if (tty->line_length) push_input(index, tty->line, tty->line_length);
        else {
            tty->input_eof++;
            jolt_wake_all();
        }
        tty->line_length = 0;
        return;
    }
    if (tty->line_length < JOLT_TTY_LINE - 1 || character == '\n') {
        if (tty->line_length < JOLT_TTY_LINE) tty->line[tty->line_length++] = character;
        echo_character(index, character);
    }
    if (character == '\n' || (value && value == control[LINUX_VEOL])) {
        push_input(index, tty->line, tty->line_length);
        tty->line_length = 0;
    }
}

static void process_input(int index, char character) {
    const struct linux_termios *settings = &tty_at(index)->termios;
    if (character == '\r' && (settings->input_flags & LINUX_IGNCR)) return;
    character = translate_input(settings, character);
    if (settings->local_flags & LINUX_ISIG) {
        int signal = signal_for(settings, (BYTE)character);
        if (signal) {
            raise_signal(index, character, signal);
            return;
        }
    }
    if (settings->local_flags & LINUX_ICANON) {
        canonical_input(index, character);
        return;
    }
    echo_character(index, character);
    push_input(index, &character, 1);
}

void jolt_tty_input(int index, char character) {
    jolt_lock();
    process_input(index, character);
    jolt_unlock();
}

DWORD jolt_tty_master_write(int index, const char *bytes, DWORD length) {
    jolt_lock();
    for (DWORD position = 0; position < length; position++) process_input(index, bytes[position]);
    jolt_unlock();
    return length;
}

void jolt_tty_resize(int index, WORD rows, WORD columns) {
    jolt_lock();
    struct jolt_tty *tty = tty_at(index);
    int changed = tty->rows != rows || tty->columns != columns;
    tty->rows = rows;
    tty->columns = columns;
    if (changed && tty->foreground) jolt_signal_group(tty->foreground, LINUX_SIGWINCH);
    jolt_unlock();
}

static void release_tty(int index) {
    struct jolt_tty *tty = tty_at(index);
    if (index == JOLT_CONSOLE || tty->masters > 0 || tty->slaves > 0) return;
    for (int slot = 0; slot < JOLT_PROCESSES; slot++)
        if (jolt.kernel->processes[slot].ctty == index) jolt.kernel->processes[slot].ctty = JOLT_NO_TTY;
    tty->state = TTY_FREE;
}

void jolt_tty_hang_up(int index) {
    struct jolt_tty *tty = tty_at(index);
    tty->hung_up = 1;
    if (tty->foreground) {
        jolt_signal_group(tty->foreground, LINUX_SIGHUP);
        jolt_signal_group(tty->foreground, LINUX_SIGCONT);
    }
    jolt_wake_all();
    release_tty(index);
}

void jolt_tty_release_end(int index, int master) {
    struct jolt_tty *tty = tty_at(index);
    if (index == JOLT_CONSOLE) return;
    if (master) {
        if (--tty->masters <= 0) jolt_tty_hang_up(index);
        return;
    }
    if (--tty->slaves <= 0) {
        jolt_wake_all();
        release_tty(index);
    }
}
