#pragma once
#include <windows.h>

#define THEME_ANSI_COLOURS 16
#define THEME_NAME_CAPACITY 32
#define THEME_DEFAULT "classic"

struct theme {
    COLORREF foreground;
    COLORREF background;
    COLORREF ansi[THEME_ANSI_COLOURS];
    int bold_bright;
};

int theme_load(const char *name, int dark, struct theme *theme);
int theme_valid_name(const char *name);
