#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "script.h"
#include "session.h"
#include "display.h"
#include "broker.h"
#include "tty.h"

#define SCRIPT_CAPACITY 8192
#define CHUNK_CAPACITY 512
#define QUIET_INTERVAL 400
#define SCREEN_CAPACITY 4096
#define ESCAPE 27

struct script_state {
    char text[SCRIPT_CAPACITY];
    DWORD length, position;
    int loaded, done, forced, screen_recorded;
    DWORD resume_at;
};

static struct script_state script;

int script_load(const WCHAR *name) {
    WCHAR path[MAX_PATH];
    if (name[0] == L'\\') wcscpy(path, name);
    else session_path(path, name);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    ReadFile(file, script.text, sizeof script.text, &script.length, NULL);
    CloseHandle(file);
    script.loaded = 1;
    return 1;
}

int script_active(void) {
    return script.loaded;
}

static int at_directive(const char *name) {
    size_t length = strlen(name);
    return script.position + length < script.length && !strncmp(script.text + script.position, name, length);
}

static void skip_line(void) {
    while (script.position < script.length && script.text[script.position++] != '\n') {}
}

static int run_directive(void) {
    if (at_directive("@wait ")) {
        script.position += strlen("@wait ");
        script.resume_at = GetTickCount() + (DWORD)atoi(script.text + script.position);
        skip_line();
        return 1;
    }
    if (at_directive("@scroll ")) {
        script.position += strlen("@scroll ");
        display_scroll_to(atoi(script.text + script.position));
        skip_line();
        return 1;
    }
    return 0;
}

static int hex_value(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

static int read_escape(char *character, int *newline) {
    char escaped = script.text[script.position++];
    switch (escaped) {
    case 'e': *character = ESCAPE; return 1;
    case 'r': *character = '\r'; return 1;
    case 'n': *character = '\n'; return 1;
    case 't': *character = '\t'; return 1;
    case 'c': *newline = 0; return 0;
    case 'x':
        if (script.position + 1 < script.length && hex_value(script.text[script.position]) >= 0 &&
            hex_value(script.text[script.position + 1]) >= 0) {
            *character = (char)(hex_value(script.text[script.position]) * 16 + hex_value(script.text[script.position + 1]));
            script.position += 2;
            return 1;
        }
        break;
    }
    *character = escaped;
    return 1;
}

static DWORD read_chunk(char *chunk) {
    DWORD length = 0;
    int newline = 1;
    while (script.position < script.length && script.text[script.position] != '\n' && length < CHUNK_CAPACITY - 1) {
        char character = script.text[script.position++];
        if (character == '\r') continue;
        if (character == '\\' && script.position < script.length && !read_escape(&character, &newline)) continue;
        chunk[length++] = character;
    }
    if (script.position < script.length) script.position++;
    if (newline) chunk[length++] = '\r';
    return length;
}

static void record_screen(void) {
    if (script.screen_recorded) return;
    char text[SCREEN_CAPACITY];
    DWORD length = display_describe(text, sizeof text);
    length += display_screen_text(text + length, sizeof text - length);
    session_write_file(L"JoltScreen.ready", text, length, NULL, 0);
    script.screen_recorded = 1;
}

void script_step(void) {
    if (!script.loaded || script.done || session.finished) return;
    if (script.resume_at) {
        if ((LONG)(GetTickCount() - script.resume_at) < 0) return;
        script.resume_at = 0;
        script.forced = 1;
    }
    if (run_directive()) return;
    if (!script.forced && !broker_idle(QUIET_INTERVAL)) return;
    script.forced = 0;
    if (script.position >= script.length) {
        script.done = 1;
        record_screen();
        return;
    }
    char chunk[CHUNK_CAPACITY];
    tty_type(chunk, read_chunk(chunk));
}
