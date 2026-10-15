#pragma once
#include <windows.h>

#define BITMAP_FONT_HEIGHTS 8

struct bitmap_font {
    int width, height, count;
    DWORD *codepoints;
    WORD *rows[2];
};

int bitmap_font_heights(const char *face, int *heights);
int bitmap_font_pick(const char *face, int requested);
int bitmap_font_step(const char *face, int current, int direction);
int bitmap_font_load(const char *face, int height, struct bitmap_font *font);
void bitmap_font_free(struct bitmap_font *font);
int bitmap_font_index(const struct bitmap_font *font, WCHAR character);
void bitmap_font_render(const struct bitmap_font *font, WORD *target, int pitch, int index, int bold, WORD foreground,
    WORD background, int underline);
