#include <windows.h>
#include <shellapi.h>
#include <string.h>
#include <wchar.h>
#include "hotkeys.h"
#include "session.h"
#include "tty.h"

#define HOTKEYS_FILE L"root\\etc\\hotkeys"
#define HOTKEYS_CAPACITY 4096
#define SHELL_KEYS L"Software\\Microsoft\\Shell\\Keys\\"
#define SHELL_KEY_PREFIX 0x40
#define SHELL_KEY_CAPACITY 64

struct button {
    DWORD key;
    const char *name;
};

static const struct button buttons[] = {
    { 0xc1, "1" }, { 0xc2, "2" }, { 0xc3, "3" }, { 0xc4, "4" }, { 0xc5, "5" }, { 0xc6, "6" },
    { 0xc7, "7" }, { 0xc8, "8" }, { 0xc9, "9" }, { 0xca, "10" }, { 0xcb, "11" },
    { 0xd0, "player" }, { 0xd1, "volume-up" }, { 0xd2, "volume-down" }, { 0xd3, "play-pause" },
};

int hotkeys_engaged(const struct display_settings *settings) {
    return settings->hotkeys == HOTKEYS_ALWAYS || (settings->hotkeys == HOTKEYS_FULLSCREEN && settings->fullscreen);
}

static const char *button_name(WPARAM key) {
    for (size_t index = 0; index < sizeof buttons / sizeof buttons[0]; index++)
        if (buttons[index].key == key) return buttons[index].name;
    return NULL;
}

static int read_hotkeys(char *text) {
    WCHAR path[MAX_PATH];
    session_path(path, HOTKEYS_FILE);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    DWORD count = 0;
    ReadFile(file, text, HOTKEYS_CAPACITY - 1, &count, NULL);
    CloseHandle(file);
    text[count] = 0;
    return 1;
}

static int find_command(const char *text, const char *name, char *command) {
    size_t length = strlen(name);
    for (const char *line = text; *line;) {
        const char *end = line + strcspn(line, "\r\n");
        if (!strncmp(line, name, length) && (line[length] == ' ' || line[length] == '\t')) {
            const char *start = line + length;
            while (*start == ' ' || *start == '\t') start++;
            size_t size = (size_t)(end - start);
            if (size >= HOTKEYS_CAPACITY) size = HOTKEYS_CAPACITY - 1;
            memcpy(command, start, size);
            command[size] = 0;
            return size > 0;
        }
        line = *end ? end + 1 : end;
    }
    return 0;
}

static void launch_default(WPARAM key) {
    WCHAR key_path[SHELL_KEY_CAPACITY], target[MAX_PATH];
    HKEY shell_key;
    wsprintfW(key_path, L"%s%02X%02X", SHELL_KEYS, SHELL_KEY_PREFIX, (unsigned)key);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key_path, 0, 0, &shell_key) != ERROR_SUCCESS) return;
    DWORD type = 0, size = sizeof target;
    LONG result = RegQueryValueExW(shell_key, NULL, NULL, &type, (BYTE *)target, &size);
    if (result != ERROR_SUCCESS) {
        size = sizeof target;
        result = RegQueryValueExW(shell_key, L"Default", NULL, &type, (BYTE *)target, &size);
    }
    RegCloseKey(shell_key);
    if (result != ERROR_SUCCESS || type != REG_SZ) return;
    target[MAX_PATH - 1] = 0;
    SHELLEXECUTEINFO information;
    memset(&information, 0, sizeof information);
    information.cbSize = sizeof information;
    information.hwnd = session.window;
    information.lpFile = target;
    information.nShow = SW_SHOWNORMAL;
    ShellExecuteEx(&information);
}

int hotkeys_press(WPARAM key) {
    const char *name = button_name(key);
    if (!name) return 0;
    static char text[HOTKEYS_CAPACITY], command[HOTKEYS_CAPACITY];
    if (read_hotkeys(text) && find_command(text, name, command)) {
        tty_type(command, (DWORD)strlen(command));
        tty_type("\r", 1);
    } else {
        launch_default(key);
    }
    return 1;
}
