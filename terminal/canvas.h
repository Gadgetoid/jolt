#pragma once
#include <windows.h>

struct canvas_cell {
    WCHAR character;
    WORD style;
    COLORREF foreground, background;
};

struct canvas {
    HBITMAP bitmap;
    HDC dc;
    WORD *pixels;
    int width, height, pitch;
    struct canvas_cell *cells;
    int rows, columns;
};

int canvas_resize(struct canvas *canvas, int width, int height, int rows, int columns);
void canvas_free(struct canvas *canvas);
void canvas_forget(struct canvas *canvas);
void canvas_fill(struct canvas *canvas, COLORREF colour);
int canvas_update_cell(struct canvas *canvas, int row, int column, const struct canvas_cell *cell);
void canvas_move(struct canvas *canvas, int origin_x, int origin_y, int cell_width, int cell_height, int top, int bottom,
    int left, int right, int downward, int rightward);
WORD *canvas_pixel(struct canvas *canvas, int x, int y);
WORD canvas_colour(COLORREF colour);
COLORREF canvas_rgb(WORD colour);
