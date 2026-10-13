#pragma once
#include <windows.h>

#define STYLE_BOLD 1
#define STYLE_UNDERLINE 2
#define STYLE_REVERSE 4
#define STYLE_DEFAULT_FOREGROUND 8
#define STYLE_DEFAULT_BACKGROUND 16

struct styled_cell {
    WCHAR character;
    BYTE style;
    COLORREF foreground;
    COLORREF background;
};

int scrollback_init(void);
void scrollback_free(void);
int scrollback_count(void);
void scrollback_push(int columns, const struct styled_cell *cells);
int scrollback_pop(int columns, struct styled_cell *cells);
void scrollback_cell(int line, int column, struct styled_cell *cell);
