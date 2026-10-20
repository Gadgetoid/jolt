#pragma once
#include <stdint.h>

int boxdraw_covers(uint32_t character);
void boxdraw_render(uint16_t *target, int pitch, int width, int height, int left, int top, uint32_t character, int bold,
    uint16_t foreground, uint16_t background, int underline);
