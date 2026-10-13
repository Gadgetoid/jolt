#pragma once
#include <windows.h>

#define THEME_ANSI_COLOURS 16

enum theme_name { THEME_CLASSIC, THEME_SOLARIZED };

struct theme {
    COLORREF foreground;
    COLORREF background;
    COLORREF ansi[THEME_ANSI_COLOURS];
};

const struct theme *theme_lookup(enum theme_name name, int dark);
int theme_parse(const char *text, enum theme_name *name);
const char *theme_label(enum theme_name name);
