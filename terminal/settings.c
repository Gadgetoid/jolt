#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "session.h"
#include "keyboard.h"
#include "hotkeys.h"

#define SETTINGS_FILE L"jolt.cfg"
#define SETTINGS_CAPACITY 256
#define FONT_MINIMUM 8
#define FONT_MAXIMUM 32
#define FONT_DEFAULT 16

static struct display_settings current = { 0, 0, FONT_DEFAULT, THEME_CLASSIC, HOTKEYS_FULLSCREEN };
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

static void save(void) {
    char text[SETTINGS_CAPACITY];
    int length = snprintf(text, sizeof text, "fullscreen=%d\r\ndark=%d\r\nfont=%d\r\ntheme=%s\r\nhotkeys=%s\r\n",
        current.fullscreen, current.dark, current.font_height, theme_label(current.theme), hotkey_modes[current.hotkeys]);
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
        const char *hotkeys = value_of(text, "hotkeys=");
        if (hotkeys) parse_hotkey_mode(hotkeys, &current.hotkeys);
        const char *theme = value_of(text, "theme=");
        if (theme) {
            char name[16];
            size_t length = strcspn(theme, "\r\n");
            if (length < sizeof name) {
                memcpy(name, theme, length);
                name[length] = 0;
                theme_parse(name, &current.theme);
            }
        }
    }
    *settings = current;
}

static int apply_command(const char *command) {
    if (!strcmp(command, "fullscreen")) current.fullscreen = 1;
    else if (!strcmp(command, "window")) current.fullscreen = 0;
    else if (!strcmp(command, "toggle")) current.fullscreen = !current.fullscreen;
    else if (!strcmp(command, "dark")) current.dark = 1;
    else if (!strcmp(command, "light")) current.dark = 0;
    else if (!strncmp(command, "font=", 5) && valid_font(atoi(command + 5))) current.font_height = atoi(command + 5);
    else if (!strncmp(command, "theme=", 6)) return theme_parse(command + 6, &current.theme);
    else if (!strncmp(command, "hotkeys=", 8)) return parse_hotkey_mode(command + 8, &current.hotkeys);
    else return 0;
    return 1;
}

void settings_command(const char *command) {
    if (apply_command(command)) {
        display_apply(&current);
        save();
    }
    keyboard_set_hotkeys(hotkeys_engaged(&current));
}
