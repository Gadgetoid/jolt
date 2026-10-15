#include <windows.h>
#include <shellapi.h>
#include <string.h>
#include <wchar.h>
#include "optional.h"
#include "desktop.h"
#include "session.h"
#include "kernel.h"
#include "hostpath.h"
#include "text.h"
#include "display.h"

#define CLIPBOARD_LIMIT 262144
#define CLIPBOARD_PATTERN L"root\\tmp\\.clipboard-*"
#define TEMPORARY_FOLDER L"root\\tmp\\"
#define WIDE_UNITS_PER_BYTE 2
#define SHORTCUT_TARGET_LIMIT 256
#define SHORTCUT_FIELD_CAPACITY 384
#define SHORTCUT_START_MENU 's'
#define SHELL_FOLDERS_KEY L"System\\Explorer\\Shell Folders"

typedef BOOL (WINAPI *create_shortcut_function)(LPWSTR, LPWSTR);

static BOOL create_shortcut(WCHAR *shortcut, WCHAR *target) {
    create_shortcut_function create = (create_shortcut_function)jolt_optional(L"SHCreateShortcut");
    return create && create(shortcut, target);
}

static void guest_root(WCHAR *path) {
    session_path(path, L"root");
}

void desktop_clipboard_cleanup(void) {
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW found;
    session_path(pattern, CLIPBOARD_PATTERN);
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        session_path(path, TEMPORARY_FOLDER);
        if (wcslen(path) + wcslen(found.cFileName) < MAX_PATH) {
            wcscat(path, found.cFileName);
            DeleteFileW(path);
        }
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

static char *read_file(const WCHAR *path, DWORD *size) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    DWORD length = GetFileSize(file, NULL);
    if (length == INVALID_FILE_SIZE || length > CLIPBOARD_LIMIT) length = CLIPBOARD_LIMIT;
    char *text = LocalAlloc(LMEM_FIXED, length + 1);
    if (text && !ReadFile(file, text, length, size, NULL)) {
        LocalFree(text);
        text = NULL;
    }
    CloseHandle(file);
    return text;
}

static void set_clipboard(const char *text, DWORD size) {
    DWORD capacity = size * WIDE_UNITS_PER_BYTE + 1;
    WCHAR *wide = LocalAlloc(LMEM_FIXED, capacity * sizeof(WCHAR));
    if (!wide) return;
    jolt_wide_from_utf8(text, size, wide, capacity);
    if (!OpenClipboard(session.window)) {
        LocalFree(wide);
        return;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, wide)) LocalFree(wide);
    CloseClipboard();
}

void desktop_clipboard_commit(void) {
    char guest[JOLT_GUEST_PATH];
    jolt_lock();
    strcpy(guest, jolt.kernel->clipboard_commit);
    jolt.kernel->clipboard_commit[0] = 0;
    jolt_unlock();
    if (!guest[0]) return;
    WCHAR root[MAX_PATH], path[MAX_PATH];
    guest_root(root);
    if (jolt_host_path(root, guest, path)) return;
    DWORD size = 0;
    char *text = read_file(path, &size);
    if (text) {
        set_clipboard(text, size);
        LocalFree(text);
    }
    desktop_clipboard_cleanup();
}

void desktop_open(const char *guest) {
    WCHAR root[MAX_PATH], path[MAX_PATH];
    guest_root(root);
    if (guest[0] != '/' || jolt_host_path(root, guest, path)) return;
    SHELLEXECUTEINFO information;
    memset(&information, 0, sizeof information);
    information.cbSize = sizeof information;
    information.hwnd = session.window;
    information.lpFile = path;
    information.nShow = SW_SHOWNORMAL;
    ShellExecuteEx(&information);
}

static int widen(const char *text, size_t length, WCHAR *wide, DWORD capacity) {
    if (length * WIDE_UNITS_PER_BYTE + 1 > capacity) return 0;
    jolt_wide_from_utf8(text, (DWORD)length, wide, capacity);
    return 1;
}

static void report(const char *text) {
    display_output(text, (DWORD)strlen(text));
}

static void shell_folder(const WCHAR *value, const WCHAR *fallback, WCHAR *folder) {
    HKEY key;
    DWORD type = 0, size = MAX_PATH * sizeof(WCHAR);
    folder[0] = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, SHELL_FOLDERS_KEY, 0, 0, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, value, NULL, &type, (BYTE *)folder, &size) != ERROR_SUCCESS || type != REG_SZ)
            folder[0] = 0;
        RegCloseKey(key);
    }
    folder[MAX_PATH - 1] = 0;
    if (!folder[0]) wcscpy(folder, fallback);
}

static int shortcut_path(char place, const WCHAR *name, WCHAR *shortcut) {
    if (place == SHORTCUT_START_MENU) shell_folder(L"Programs", L"\\Windows\\Programs", shortcut);
    else shell_folder(L"Desktop", L"\\Windows\\Desktop", shortcut);
    if (wcslen(shortcut) + wcslen(name) + 6 >= MAX_PATH) return 0;
    wcscat(shortcut, L"\\");
    wcscat(shortcut, name);
    wcscat(shortcut, L".lnk");
    return 1;
}

static int shortcut_target(const WCHAR *command, WCHAR *target) {
    WCHAR program[MAX_PATH];
    if (!GetModuleFileNameW(NULL, program, MAX_PATH)) return 0;
    if (wcslen(program) + wcslen(command) + 10 >= SHORTCUT_TARGET_LIMIT) return 0;
    wcscpy(target, L"\"");
    wcscat(target, program);
    wcscat(target, L"\" -e \"");
    wcscat(target, command);
    wcscat(target, L"\"");
    return 1;
}

void desktop_shortcut(const char *request) {
    if (!request[0] || request[1] != ';') return;
    const char *name = request + 2;
    const char *separator = strchr(name, ';');
    if (!separator || separator == name) return;
    WCHAR wide_name[SHORTCUT_FIELD_CAPACITY], wide_command[SHORTCUT_FIELD_CAPACITY];
    WCHAR shortcut[MAX_PATH], target[SHORTCUT_TARGET_LIMIT];
    if (!widen(name, (size_t)(separator - name), wide_name, SHORTCUT_FIELD_CAPACITY) ||
        !widen(separator + 1, strlen(separator + 1), wide_command, SHORTCUT_FIELD_CAPACITY)) {
        report("\r\njolt: shortcut name or command too long\r\n");
    } else if (!shortcut_path(request[0], wide_name, shortcut)) {
        report("\r\njolt: shortcut folder not found\r\n");
    } else if (!shortcut_target(wide_command, target)) {
        report("\r\njolt: shortcut command too long\r\n");
    } else if (!create_shortcut(shortcut, target)) {
        report("\r\njolt: could not create shortcut\r\n");
    }
}
