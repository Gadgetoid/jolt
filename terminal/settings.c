#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "session.h"
#include "keyboard.h"
#include "hotkeys.h"
#include "bitmapfont.h"

#define SETTINGS_FILE L"jolt.cfg"
#define SETTINGS_CAPACITY 256
#define FONT_MINIMUM 8
#define FONT_MAXIMUM 32
#define FONT_DEFAULT 16
#define FONT_STEP 2
#define DEFAULT_FACE "default"
#define MISSING_CAPACITY 96

static struct display_settings current = { 0, 0, FONT_DEFAULT, THEME_DEFAULT, HOTKEYS_FULLSCREEN, "" };
static const char *const hotkey_modes[] = { "off", "fullscreen", "always" };

static int valid_font(int height) {
    return height >= FONT_MINIMUM && height <= FONT_MAXIMUM;
}

static const char *value_of(const char *text, const char *key) {
    const char *found = strstr(text, key);
    return found ? found + strlen(key) : NULL;
}

static int parse_hotkey_mode(const char *text, enum hotkey_mode *mode) {
    for (int index = 0; index < (int)(sizeof hotkey_modes / sizeof hotkey_modes[0]); index++) {
        size_t length = strlen(hotkey_modes[index]);
        if (!strncmp(text, hotkey_modes[index], length) && (!text[length] || text[length] == '\r' || text[length] == '\n')) {
            *mode = (enum hotkey_mode)index;
            return 1;
        }
    }
    return 0;
}

static int set_theme(const char *text) {
    char name[THEME_NAME_CAPACITY];
    size_t length = strcspn(text, "\r\n");
    if (length >= sizeof name) return 0;
    memcpy(name, text, length);
    name[length] = 0;
    if (!theme_valid_name(name)) return 0;
    strcpy(current.theme, name);
    return 1;
}

static int set_face(const char *text) {
    size_t length = strcspn(text, "\r\n");
    if (length >= sizeof current.face) return 0;
    if (length == strlen(DEFAULT_FACE) && !strncmp(text, DEFAULT_FACE, length)) length = 0;
    memcpy(current.face, text, length);
    current.face[length] = 0;
    return 1;
}

static void save(void) {
    char text[SETTINGS_CAPACITY];
    int length = snprintf(text, sizeof text,
        "fullscreen=%d\r\ndark=%d\r\nfont=%d\r\ntheme=%s\r\nhotkeys=%s\r\nface=%s\r\n",
        current.fullscreen, current.dark, current.font_height, current.theme, hotkey_modes[current.hotkeys],
        current.face);
    if (length > 0) session_write_file(SETTINGS_FILE, text, (DWORD)length, NULL, 0);
}

void settings_load(struct display_settings *settings) {
    WCHAR path[MAX_PATH];
    session_path(path, SETTINGS_FILE);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        char text[SETTINGS_CAPACITY];
        DWORD count = 0;
        ReadFile(file, text, sizeof text - 1, &count, NULL);
        CloseHandle(file);
        text[count] = 0;
        current.fullscreen = strstr(text, "fullscreen=1") != NULL;
        current.dark = strstr(text, "dark=1") != NULL;
        const char *font = value_of(text, "font=");
        if (font && valid_font(atoi(font))) current.font_height = atoi(font);
        const char *face = value_of(text, "face=");
        if (face) set_face(face);
        const char *hotkeys = value_of(text, "hotkeys=");
        if (hotkeys) parse_hotkey_mode(hotkeys, &current.hotkeys);
        const char *theme = value_of(text, "theme=");
        if (theme) set_theme(theme);
    }
    *settings = current;
}

static int step_font(int step) {
    int heights[BITMAP_FONT_HEIGHTS];
    if (current.face[0] && bitmap_font_heights(current.face, heights)) {
        int next = bitmap_font_step(current.face, current.font_height, step > 0 ? 1 : -1);
        if (!next) return 0;
        current.font_height = next;
        return 1;
    }
    if (!valid_font(current.font_height + step)) return 0;
    current.font_height += step;
    return 1;
}

static int apply_command(const char *command) {
    if (!strcmp(command, "fullscreen")) current.fullscreen = 1;
    else if (!strcmp(command, "window")) current.fullscreen = 0;
    else if (!strcmp(command, "toggle")) current.fullscreen = !current.fullscreen;
    else if (!strcmp(command, "dark")) current.dark = 1;
    else if (!strcmp(command, "light")) current.dark = 0;
    else if (!strcmp(command, "font+")) return step_font(FONT_STEP);
    else if (!strcmp(command, "font-")) return step_font(-FONT_STEP);
    else if (!strncmp(command, "font=", 5) && valid_font(atoi(command + 5))) current.font_height = atoi(command + 5);
    else if (!strncmp(command, "theme=", 6)) return set_theme(command + 6);
    else if (!strncmp(command, "face=", 5)) return set_face(command + 5);
    else if (!strncmp(command, "hotkeys=", 8)) return parse_hotkey_mode(command + 8, &current.hotkeys);
    else return 0;
    return 1;
}

static void report_missing(const char *kind, const char *name) {
    char text[MISSING_CAPACITY];
    snprintf(text, sizeof text, "jolt: no %s called %s\r\n", kind, name);
    display_notice(text);
}

void settings_command(const char *command) {
    struct display_settings previous = current;
    if (apply_command(command)) {
        display_apply(&current);
        int face_missing = display_face_missing() && strcmp(current.face, previous.face);
        int theme_missing = display_theme_missing() && strcmp(current.theme, previous.theme);
        if (face_missing) report_missing("fixed-width font", current.face);
        if (theme_missing) report_missing("theme", current.theme);
        if (face_missing || theme_missing) {
            current = previous;
            display_apply(&current);
        }
        save();
    }
    keyboard_set_hotkeys(hotkeys_engaged(&current));
}
