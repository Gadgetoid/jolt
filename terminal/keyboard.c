#include <windows.h>
#include "keyboard.h"
#include "display.h"
#include "session.h"
#include "tty.h"

#define ASCII_ESCAPE 27
#define ASCII_BACKSPACE 8
#define ASCII_DELETE 127
#define ASCII_LIMIT 128

struct key_mapping {
    WPARAM key;
    VTermKey terminal_key;
};

static const struct key_mapping key_mappings[] = {
    { VK_ESCAPE, VTERM_KEY_ESCAPE },
    { VK_UP, VTERM_KEY_UP },
    { VK_DOWN, VTERM_KEY_DOWN },
    { VK_RIGHT, VTERM_KEY_RIGHT },
    { VK_LEFT, VTERM_KEY_LEFT },
    { VK_HOME, VTERM_KEY_HOME },
    { VK_END, VTERM_KEY_END },
    { VK_INSERT, VTERM_KEY_INS },
    { VK_DELETE, VTERM_KEY_DEL },
    { VK_PRIOR, VTERM_KEY_PAGEUP },
    { VK_NEXT, VTERM_KEY_PAGEDOWN },
};

static VTermModifier current_modifiers(void) {
    int modifiers = VTERM_MOD_NONE;
    if (GetKeyState(VK_SHIFT) < 0) modifiers |= VTERM_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) < 0) modifiers |= VTERM_MOD_CTRL;
    if (GetKeyState(VK_MENU) < 0) modifiers |= VTERM_MOD_ALT;
    return (VTermModifier)modifiers;
}

static int scrollback_key(WPARAM key) {
    if (GetKeyState(VK_SHIFT) >= 0) return 0;
    int page = display_page_lines();
    switch (key) {
    case VK_PRIOR: display_scroll_by(page); return 1;
    case VK_NEXT: display_scroll_by(-page); return 1;
    case VK_UP: display_scroll_by(1); return 1;
    case VK_DOWN: display_scroll_by(-1); return 1;
    default: return 0;
    }
}

static int is_modifier(WPARAM key) {
    return key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU;
}

void keyboard_key_down(WPARAM key) {
    if (session.finished) {
        if (key == VK_ESCAPE) display_close();
        return;
    }
    if (scrollback_key(key)) return;
    if (!is_modifier(key)) display_scroll_to(0);
    for (size_t index = 0; index < sizeof key_mappings / sizeof key_mappings[0]; index++) {
        if (key_mappings[index].key != key) continue;
        display_key(key_mappings[index].terminal_key, current_modifiers());
        return;
    }
}

void keyboard_character(WPARAM character) {
    if (session.finished || !character || character >= ASCII_LIMIT || character == ASCII_ESCAPE) return;
    tty_input(character == ASCII_BACKSPACE ? ASCII_DELETE : (char)character);
}
