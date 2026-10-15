#pragma once
#include <windows.h>
#include <vterm.h>
#include "theme.h"

enum hotkey_mode { HOTKEYS_OFF, HOTKEYS_FULLSCREEN, HOTKEYS_ALWAYS };

struct display_settings {
    int fullscreen;
    int dark;
    int font_height;
    char theme[THEME_NAME_CAPACITY];
    enum hotkey_mode hotkeys;
    char face[LF_FACESIZE];
};

typedef void (*display_command_handler)(const char *command);

int display_create(HWND window, const struct display_settings *settings, display_command_handler handler);
void display_destroy(void);
void display_apply(const struct display_settings *settings);
void display_output(const char *bytes, DWORD count);
void display_notice(const char *text);
void display_note_key(void);
void display_report_latency(void);
void display_drain_tty(void);
void display_paint(void);
void display_resize(void);
void display_scroll_by(int lines);
void display_scroll_to(int lines_back);
void display_scrollbar(WPARAM request);
int display_page_lines(void);
DWORD display_screen_text(char *buffer, DWORD capacity);
DWORD display_describe(char *buffer, DWORD capacity);
void display_show_splash(const WCHAR *path);
int display_splash_loaded(void);
void display_close(void);
void display_key(VTermKey key, VTermModifier modifiers);
void display_restore_taskbar(void);
int display_face_available(const char *face);
