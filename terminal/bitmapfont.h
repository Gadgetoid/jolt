#pragma once
#include <windows.h>

#define BITMAP_FONT_HEIGHTS 8

struct bitmap_font {
    int width, height, columns, count, stride;
    DWORD *codepoints;
    BYTE *bits[2];
};

struct pixel_row {
    HBITMAP bitmap;
    HDC dc;
    WORD *pixels;
    int width, height, pitch;
};

int bitmap_font_heights(const char *face, int *heights);
int bitmap_font_pick(const char *face, int requested);
int bitmap_font_step(const char *face, int current, int direction);
int bitmap_font_load(const char *face, int height, struct bitmap_font *font);
void bitmap_font_free(struct bitmap_font *font);
int bitmap_font_index(const struct bitmap_font *font, WCHAR character);

int pixel_row_create(struct pixel_row *row, int width, int height);
void pixel_row_free(struct pixel_row *row);
WORD pixel_colour(COLORREF colour);
void pixel_row_glyph(struct pixel_row *row, const struct bitmap_font *font, int x, int index, int bold, WORD foreground,
    WORD background, int underline);
