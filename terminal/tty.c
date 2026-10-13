#include <windows.h>
#include <string.h>
#include "tty.h"
#include "kernel.h"

#define LINE_CAPACITY 256
#define CONTROL(letter) ((letter) & 0x1f)
#define ASCII_DELETE 0x7f

static char line[LINE_CAPACITY];
static DWORD line_length;

int tty_line_pending(void) {
    return line_length != 0;
}

void tty_default_settings(struct linux_termios *settings) {
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

static const struct linux_termios *settings(void) {
    return &jolt.kernel->termios;
}

static int is_control(BYTE value) {
    return value < 32 || value == ASCII_DELETE;
}

static void echo(const char *bytes, DWORD length) {
    jolt_tty_write(bytes, length, 0);
}

static void echo_character(char character) {
    DWORD local = settings()->local_flags;
    if (!(local & LINUX_ECHO)) return;
    BYTE value = (BYTE)character;
    if ((local & LINUX_ECHOCTL) && is_control(value) && character != '\n' && character != '\t') {
        char caret[2] = { '^', (char)(value == ASCII_DELETE ? '?' : value + '@') };
        echo(caret, sizeof caret);
    } else {
        echo(&character, 1);
    }
}

static void push_input(const char *bytes, DWORD length) {
    struct jolt_kernel *kernel = jolt.kernel;
    jolt_lock();
    DWORD written = (DWORD)kernel->input_written;
    for (DWORD index = 0; index < length; index++) {
        if (written - (DWORD)kernel->input_read >= JOLT_INPUT_SIZE) break;
        kernel->input[written++ % JOLT_INPUT_SIZE] = bytes[index];
    }
    InterlockedExchange(&kernel->input_written, (LONG)written);
    jolt_wake_all();
    jolt_unlock();
}

static void push_end_of_file(void) {
    jolt_lock();
    jolt.kernel->input_eof++;
    jolt_wake_all();
    jolt_unlock();
}

static void discard_input(void) {
    jolt_lock();
    jolt.kernel->input_read = jolt.kernel->input_written;
    jolt.kernel->input_eof = 0;
    jolt_unlock();
}

static void erase_characters(DWORD count) {
    DWORD local = settings()->local_flags;
    int visible = (local & LINUX_ECHO) && (local & LINUX_ECHOE);
    while (count-- && line_length) {
        BYTE value = (BYTE)line[--line_length];
        if (!visible) continue;
        int width = (local & LINUX_ECHOCTL) && is_control(value) && value != '\t' ? 2 : 1;
        while (width--) echo("\b \b", 3);
    }
}

static void erase_word(void) {
    while (line_length && line[line_length - 1] == ' ') erase_characters(1);
    while (line_length && line[line_length - 1] != ' ') erase_characters(1);
}

static char translate_input(char character) {
    DWORD input = settings()->input_flags;
    if (input & LINUX_ISTRIP) character &= 0x7f;
    if (character == '\r' && (input & LINUX_ICRNL)) return '\n';
    if (character == '\n' && (input & LINUX_INLCR)) return '\r';
    return character;
}

static int signal_for(BYTE value) {
    const BYTE *control = settings()->control;
    if (!value) return 0;
    if (value == control[LINUX_VINTR]) return LINUX_SIGINT;
    if (value == control[LINUX_VQUIT]) return LINUX_SIGQUIT;
    if (value == control[LINUX_VSUSP]) return LINUX_SIGTSTP;
    return 0;
}

static void raise_signal(char character, int signal) {
    line_length = 0;
    if (signal != LINUX_SIGTSTP) discard_input();
    echo_character(character);
    jolt_lock();
    jolt_signal_group(jolt.kernel->foreground, signal);
    jolt_unlock();
}

static void canonical_input(char character) {
    const BYTE *control = settings()->control;
    BYTE value = (BYTE)character;
    if (value == control[LINUX_VERASE] || value == '\b') {
        erase_characters(1);
        return;
    }
    if (value == control[LINUX_VKILL]) {
        erase_characters(line_length);
        return;
    }
    if (value == control[LINUX_VWERASE]) {
        erase_word();
        return;
    }
    if (value == control[LINUX_VEOF]) {
        if (line_length) push_input(line, line_length);
        else push_end_of_file();
        line_length = 0;
        return;
    }
    if (line_length < LINE_CAPACITY - 1 || character == '\n') {
        if (line_length < LINE_CAPACITY) line[line_length++] = character;
        echo_character(character);
    }
    if (character == '\n' || (value && value == control[LINUX_VEOL])) {
        push_input(line, line_length);
        line_length = 0;
    }
}

void tty_input(char character) {
    if (character == '\r' && (settings()->input_flags & LINUX_IGNCR)) return;
    character = translate_input(character);
    if (settings()->local_flags & LINUX_ISIG) {
        int signal = signal_for((BYTE)character);
        if (signal) {
            raise_signal(character, signal);
            return;
        }
    }
    if (settings()->local_flags & LINUX_ICANON) {
        canonical_input(character);
        return;
    }
    echo_character(character);
    push_input(&character, 1);
}

void tty_type(const char *bytes, DWORD length) {
    for (DWORD index = 0; index < length; index++) tty_input(bytes[index]);
}
