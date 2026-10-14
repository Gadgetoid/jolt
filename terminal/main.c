#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "session.h"
#include "display.h"
#include "settings.h"
#include "broker.h"
#include "script.h"
#include "keyboard.h"
#include "tty.h"
#include "sound.h"
#include "desktop.h"
#include "hotkeys.h"
#include "text.h"
#include "kernel.h"
#include "guest-manifest.h"

#define WINDOW_CLASS L"JoltTerminal"
#define WINDOW_TITLE L"JoLT / Jornada Linux Terminal"
#define TIMER_ID 1
#define TIMER_MILLISECONDS 100
#define SCRIPT_LIMIT_MILLISECONDS 180000
#define KERNEL_OPEN_ATTEMPTS 8
#define IDENTIFIER_MIXER 2654435761UL
#define VTERM_ABORT_CODE 125
#define ICON_RESOURCE 1
#define COMMAND_FLAG L"-e "
#define EXIT_STATUS_SHIFT 8
#define EXIT_STATUS_MASK 0xff
#define SIGNAL_MASK 0x7f
#define SMALL_ICON_SIZE 16

void jolt_vterm_exit(int code) {
    ExitProcess((UINT)code);
}

void jolt_vterm_abort(void) {
    jolt_vterm_exit(VTERM_ABORT_CODE);
}

static void write_failure(const char *text) {
    session_write_file(L"Jolt.log", text, (DWORD)strlen(text), NULL, 0);
}

static int command_failed(void) {
    return session.command[0] && !session.quiet && !session.refused && session.init_status;
}

static void report_command_failure(void) {
    char text[MESSAGE_CAPACITY];
    DWORD status = session.init_status;
    if (status & SIGNAL_MASK) snprintf(text, sizeof text, "\r\n[killed by signal %lu] Press Escape to close.", status & SIGNAL_MASK);
    else snprintf(text, sizeof text, "\r\n[exited with status %lu] Press Escape to close.", (status >> EXIT_STATUS_SHIFT) & EXIT_STATUS_MASK);
    display_output(text, (DWORD)strlen(text));
}

static void end_session(void) {
    display_drain_tty();
    broker_stop();
    session_save_result();
    KillTimer(session.window, TIMER_ID);
    InvalidateRect(session.window, NULL, FALSE);
    if (command_failed()) report_command_failure();
    else if (session.quiet || !session.refused) display_close();
}

static void on_timer(void) {
    display_drain_tty();
    if (!session.refused) broker_monitor();
    script_step();
    if (script_active()) {
        SystemIdleTimerReset();
        if (GetTickCount() - session.start_time > SCRIPT_LIMIT_MILLISECONDS)
            session_finish(LINUX_SIGKILL, "FAIL: script deadline");
    }
    if (session.finished) end_session();
}

static void on_destroy(void) {
    keyboard_set_active(0);
    KillTimer(session.window, TIMER_ID);
    display_restore_taskbar();
    if (!session.finished) {
        session_finish(LINUX_SIGHUP, "closed");
        broker_stop();
        session_save_result();
    }
    PostQuitMessage(0);
}

static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    keyboard_log(message, wparam, lparam);
    switch (message) {
    case WM_PAINT:
        display_paint();
        return 0;
    case WM_SIZE:
        display_resize();
        return 0;
    case JOLT_TTY_MESSAGE:
        InterlockedExchange(&jolt.kernel->ttys[JOLT_CONSOLE].output_notified, 0);
        display_drain_tty();
        return 0;
    case JOLT_CLIPBOARD_MESSAGE:
        desktop_clipboard_commit();
        return 0;
    case JOLT_SOUND_MESSAGE:
        sound_tone((DWORD)wparam, LOWORD(lparam), (short)HIWORD(lparam));
        return 0;
    case WM_TIMER:
        on_timer();
        return 0;
    case WM_KEYDOWN:
        keyboard_key_down(wparam);
        return 0;
    case WM_CHAR:
        keyboard_character(wparam);
        return 0;
    case WM_SYSKEYDOWN:
        if (keyboard_system_key(wparam)) return 0;
        break;
    case WM_SYSCHAR:
        if (keyboard_system_character()) return 0;
        break;
    case WM_ACTIVATE:
        keyboard_set_active(LOWORD(wparam) != WA_INACTIVE);
        break;
    case WM_VSCROLL:
        display_scrollbar(wparam);
        return 0;
    case WM_CLOSE:
        display_close();
        return 0;
    case WM_DESTROY:
        on_destroy();
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void run_command(const char *command) {
    if (!strncmp(command, "open=", strlen("open="))) desktop_open(command + strlen("open="));
    else if (!strncmp(command, "shortcut=", strlen("shortcut="))) desktop_shortcut(command + strlen("shortcut="));
    else if (!strcmp(command, "keys")) keyboard_start_log();
    else settings_command(command);
}

static int locate_folder(void) {
    DWORD length = GetModuleFileNameW(NULL, session.folder, MAX_PATH);
    WCHAR *filename = length && length < MAX_PATH - 32 ? wcsrchr(session.folder, L'\\') : NULL;
    if (!filename) return 0;
    filename[1] = 0;
    return 1;
}

static int open_kernel(void) {
    DWORD identifier = GetCurrentProcessId();
    for (int attempt = 0; attempt < KERNEL_OPEN_ATTEMPTS; attempt++) {
        if (jolt_kernel_open(identifier, 1, JOLT_GUEST_SIZE)) return 1;
        jolt_kernel_close();
        identifier = identifier * IDENTIFIER_MIXER + GetTickCount() + (DWORD)attempt;
    }
    return 0;
}

static int has_flag(const WCHAR *command_line, const WCHAR *flag) {
    return command_line && wcsstr(command_line, flag) != NULL;
}

static void load_script_argument(const WCHAR *command_line) {
    const WCHAR *argument = command_line ? wcsstr(command_line, L"--script=") : NULL;
    if (!argument) return;
    argument += wcslen(L"--script=");
    WCHAR name[MAX_PATH];
    size_t count = 0;
    while (argument[count] && argument[count] != L' ' && count < MAX_PATH - 1) {
        name[count] = argument[count];
        count++;
    }
    name[count] = 0;
    script_load(name);
}

static const WCHAR *find_command_flag(const WCHAR *command_line) {
    for (const WCHAR *flag = command_line ? wcsstr(command_line, COMMAND_FLAG) : NULL; flag;
        flag = wcsstr(flag + 1, COMMAND_FLAG))
        if (flag == command_line || flag[-1] == L' ') return flag;
    return NULL;
}

static void split_options(const WCHAR *command_line, WCHAR *options, size_t capacity) {
    const WCHAR *flag = find_command_flag(command_line);
    size_t length = command_line ? (flag ? (size_t)(flag - command_line) : wcslen(command_line)) : 0;
    if (length >= capacity) length = capacity - 1;
    if (length) wcsncpy(options, command_line, length);
    options[length] = 0;
}

static void load_command_argument(const WCHAR *command_line) {
    const WCHAR *flag = find_command_flag(command_line);
    if (!flag) return;
    WCHAR command[SESSION_COMMAND_CAPACITY];
    const WCHAR *start = flag + wcslen(COMMAND_FLAG);
    while (*start == L' ') start++;
    size_t length = wcslen(start);
    while (length && start[length - 1] == L' ') length--;
    if (length >= 2 && start[0] == L'"' && start[length - 1] == L'"') {
        start++;
        length -= 2;
    }
    if (length >= SESSION_COMMAND_CAPACITY) return;
    wcsncpy(command, start, length);
    command[length] = 0;
    DWORD used = jolt_utf8_from_wide(command, session.command, SESSION_COMMAND_CAPACITY - 1);
    session.command[used] = 0;
}

static HWND create_window(HINSTANCE instance) {
    WNDCLASSW type;
    memset(&type, 0, sizeof type);
    type.lpfnWndProc = window_procedure;
    type.hInstance = instance;
    type.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(ICON_RESOURCE));
    type.hbrBackground = GetStockObject(WHITE_BRUSH);
    type.lpszClassName = WINDOW_CLASS;
    RegisterClassW(&type);
    RECT area;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0))
        SetRect(&area, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    HWND window = CreateWindowExW(0, WINDOW_CLASS, WINDOW_TITLE, WS_VISIBLE | WS_CAPTION | WS_SYSMENU | WS_VSCROLL,
        area.left, area.top, area.right - area.left, area.bottom - area.top, NULL, NULL, instance, NULL);
    HICON small = LoadImageW(instance, MAKEINTRESOURCEW(ICON_RESOURCE), IMAGE_ICON, SMALL_ICON_SIZE, SMALL_ICON_SIZE, 0);
    if (window && small) SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)small);
    return window;
}

static void refuse(void) {
    session.refused = 1;
    session_finish(0, NULL);
    display_output(session.message, (DWORD)strlen(session.message));
    display_output("\r\nPress Escape to close.", 24);
}

static void run_message_loop(void) {
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show) {
    (void)previous;
    (void)show;
    WCHAR options[MAX_PATH];
    split_options(command_line, options, MAX_PATH);
    session.start_time = GetTickCount();
    session.memory_at_start = session_available_memory();
    session.quiet = has_flag(options, L"--quiet");
    if (!locate_folder()) return 1;
    HWND existing = FindWindowW(WINDOW_CLASS, NULL);
    if (existing) {
        if (!has_flag(options, L"--script=")) SetForegroundWindow(existing);
        write_failure("JOLT_SHELL FAIL another JoLT session is running\r\n");
        return 1;
    }
    if (!open_kernel()) {
        write_failure("JOLT_SHELL FAIL kernel objects unavailable\r\n");
        return 1;
    }
    jolt.kernel->trace = has_flag(options, L"--trace");
    jolt.kernel->launcher = GetCurrentProcessId();
    struct jolt_tty *console = &jolt.kernel->ttys[JOLT_CONSOLE];
    console->state = TTY_CONSOLE;
    jolt_tty_default_settings(&console->termios);
    load_script_argument(options);
    load_command_argument(command_line);
    session.hold = has_flag(options, L"--hold");
    session.transcript = LocalAlloc(LMEM_FIXED, TRANSCRIPT_SIZE);
    struct display_settings settings;
    settings_load(&settings);
    session.window = create_window(instance);
    if (!session.window) return 1;
    jolt.kernel->window = (DWORD)session.window;
    SetForegroundWindow(session.window);
    SetFocus(session.window);
    keyboard_set_hotkeys(hotkeys_engaged(&settings));
    if (!display_create(session.window, &settings, run_command)) {
        DestroyWindow(session.window);
        return 1;
    }
    WCHAR splash[MAX_PATH];
    session_path(splash, L"splash.bmp");
    display_show_splash(splash);
    desktop_clipboard_cleanup();
    if (!broker_load_image() || !broker_start()) refuse();
    SetTimer(session.window, TIMER_ID, TIMER_MILLISECONDS, NULL);
    run_message_loop();
    display_destroy();
    if (session.transcript) LocalFree(session.transcript);
    jolt_kernel_close();
    ExitProcess(session.finished && !session.refused && !(session.init_status & 127) ? 0 : 1);
    return 0;
}
