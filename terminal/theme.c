#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "theme.h"
#include "session.h"

#define THEMES_FOLDER L"root\\usr\\share\\themes\\"
#define THEME_EXTENSION L".conf"
#define THEME_FILE_LIMIT 8192
#define HEX_DIGITS 6

#define CLASSIC_ANSI { \
    RGB(0, 0, 0), RGB(170, 0, 0), RGB(0, 150, 0), RGB(170, 110, 0), \
    RGB(0, 0, 190), RGB(170, 0, 170), RGB(0, 150, 150), RGB(190, 190, 190), \
    RGB(90, 90, 90), RGB(240, 60, 60), RGB(40, 200, 40), RGB(220, 200, 40), \
    RGB(70, 70, 250), RGB(230, 70, 230), RGB(40, 210, 210), RGB(255, 255, 255) }

static const struct theme classic_light = { RGB(20, 20, 20), RGB(255, 255, 255), CLASSIC_ANSI, 1 };
static const struct theme classic_dark = { RGB(225, 225, 225), RGB(0, 0, 0), CLASSIC_ANSI, 1 };

int theme_valid_name(const char *name) {
    size_t length = strlen(name);
    if (!length || length >= THEME_NAME_CAPACITY) return 0;
    for (size_t index = 0; index < length; index++) {
        char character = name[index];
        int allowed = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_';
        if (!allowed) return 0;
    }
    return 1;
}

static int read_theme_file(const char *name, const WCHAR *suffix, char *text) {
    WCHAR wide[THEME_NAME_CAPACITY], relative[MAX_PATH], path[MAX_PATH];
    size_t length = strlen(name);
    for (size_t index = 0; index <= length; index++) wide[index] = (WCHAR)(unsigned char)name[index];
    wsprintfW(relative, L"%s%s%s%s", THEMES_FOLDER, wide, suffix, THEME_EXTENSION);
    session_path(path, relative);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    DWORD count = 0;
    BOOL read = ReadFile(file, text, THEME_FILE_LIMIT - 1, &count, NULL);
    CloseHandle(file);
    text[read ? count : 0] = 0;
    return read;
}

static int parse_colour(const char *text, COLORREF *colour) {
    if (*text == '#') text++;
    char digits[HEX_DIGITS + 1];
    for (int index = 0; index < HEX_DIGITS; index++) {
        char character = text[index];
        int hex = (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
            (character >= 'A' && character <= 'F');
        if (!hex) return 0;
        digits[index] = character;
    }
    digits[HEX_DIGITS] = 0;
    unsigned long value = strtoul(digits, NULL, 16);
    *colour = RGB((value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff);
    return 1;
}

static void apply_line(const char *key, size_t key_length, const char *value, struct theme *theme) {
    COLORREF colour;
    if (!parse_colour(value, &colour)) return;
    if (key_length == strlen("foreground") && !strncmp(key, "foreground", key_length)) theme->foreground = colour;
    else if (key_length == strlen("background") && !strncmp(key, "background", key_length)) theme->background = colour;
    else if (key_length > strlen("color") && !strncmp(key, "color", strlen("color"))) {
        int index = atoi(key + strlen("color"));
        if (index >= 0 && index < THEME_ANSI_COLOURS) theme->ansi[index] = colour;
    }
}

static void parse_theme(const char *text, struct theme *theme) {
    for (const char *line = text; *line;) {
        const char *end = line + strcspn(line, "\r\n");
        while (line < end && (*line == ' ' || *line == '\t')) line++;
        const char *key = line;
        while (line < end && *line != ' ' && *line != '\t') line++;
        size_t key_length = (size_t)(line - key);
        while (line < end && (*line == ' ' || *line == '\t')) line++;
        if (key_length && *key != '#') apply_line(key, key_length, line, theme);
        line = *end ? end + 1 : end;
    }
}

int theme_load(const char *name, int dark, struct theme *theme) {
    *theme = dark ? classic_dark : classic_light;
    if (!strcmp(name, THEME_DEFAULT)) return 1;
    if (!theme_valid_name(name)) return 0;
    char *text = LocalAlloc(LMEM_FIXED, THEME_FILE_LIMIT);
    if (!text) return 0;
    int found = read_theme_file(name, dark ? L"-dark" : L"-light", text) || read_theme_file(name, L"", text);
    if (found) {
        theme->bold_bright = 0;
        parse_theme(text, theme);
    }
    LocalFree(text);
    return found;
}

int theme_exists(const char *name) {
    struct theme theme;
    return theme_load(name, 1, &theme) || theme_load(name, 0, &theme);
}
