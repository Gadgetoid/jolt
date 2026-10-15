#include <windows.h>
#include <string.h>
#include "canvas.h"

#define RED_MASK 0xf800
#define GREEN_MASK 0x07e0
#define BLUE_MASK 0x001f
#define STALE_CHARACTER 0xffff

static int create_bitmap(struct canvas *canvas, int width, int height) {
    struct {
        BITMAPINFOHEADER header;
        DWORD masks[3];
    } info;
    memset(&info, 0, sizeof info);
    info.header.biSize = sizeof info.header;
    info.header.biWidth = width;
    info.header.biHeight = -height;
    info.header.biPlanes = 1;
    info.header.biBitCount = 16;
    info.header.biCompression = BI_BITFIELDS;
    info.masks[0] = RED_MASK;
    info.masks[1] = GREEN_MASK;
    info.masks[2] = BLUE_MASK;
    void *pixels = NULL;
    canvas->bitmap = CreateDIBSection(NULL, (BITMAPINFO *)&info, DIB_RGB_COLORS, &pixels, NULL, 0);
    canvas->dc = canvas->bitmap ? CreateCompatibleDC(NULL) : NULL;
    if (!canvas->bitmap || !canvas->dc || !pixels) return 0;
    SelectObject(canvas->dc, canvas->bitmap);
    canvas->pixels = pixels;
    canvas->width = width;
    canvas->height = height;
    canvas->pitch = (width * 2 + 3) / 4 * 2;
    return 1;
}

void canvas_free(struct canvas *canvas) {
    if (canvas->dc) DeleteDC(canvas->dc);
    if (canvas->bitmap) DeleteObject(canvas->bitmap);
    if (canvas->cells) LocalFree(canvas->cells);
    memset(canvas, 0, sizeof *canvas);
}

void canvas_forget(struct canvas *canvas) {
    for (int index = 0; index < canvas->rows * canvas->columns; index++) canvas->cells[index].character = STALE_CHARACTER;
}

int canvas_resize(struct canvas *canvas, int width, int height, int rows, int columns) {
    if (canvas->dc && canvas->width == width && canvas->height == height && canvas->rows == rows &&
        canvas->columns == columns) return 1;
    canvas_free(canvas);
    if (width <= 0 || height <= 0 || rows <= 0 || columns <= 0) return 0;
    canvas->cells = LocalAlloc(LMEM_FIXED, (UINT)(rows * columns) * sizeof *canvas->cells);
    if (!canvas->cells || !create_bitmap(canvas, width, height)) {
        canvas_free(canvas);
        return 0;
    }
    canvas->rows = rows;
    canvas->columns = columns;
    canvas_forget(canvas);
    return 1;
}

WORD canvas_colour(COLORREF colour) {
    return (WORD)(((GetRValue(colour) >> 3) << 11) | ((GetGValue(colour) >> 2) << 5) | (GetBValue(colour) >> 3));
}

COLORREF canvas_rgb(WORD colour) {
    BYTE red = (BYTE)((colour >> 11) << 3), green = (BYTE)(((colour >> 5) & 63) << 2), blue = (BYTE)((colour & 31) << 3);
    return RGB(red | red >> 5, green | green >> 6, blue | blue >> 5);
}

void canvas_fill(struct canvas *canvas, COLORREF colour) {
    WORD value = canvas_colour(colour);
    for (int y = 0; y < canvas->height; y++) {
        WORD *line = canvas->pixels + y * canvas->pitch;
        for (int x = 0; x < canvas->width; x++) line[x] = value;
    }
    canvas_forget(canvas);
}

WORD *canvas_pixel(struct canvas *canvas, int x, int y) {
    return canvas->pixels + y * canvas->pitch + x;
}

int canvas_update_cell(struct canvas *canvas, int row, int column, const struct canvas_cell *cell) {
    if (row < 0 || row >= canvas->rows || column < 0 || column >= canvas->columns) return 1;
    struct canvas_cell *stored = &canvas->cells[row * canvas->columns + column];
    if (stored->character == cell->character && stored->style == cell->style && stored->foreground == cell->foreground &&
        stored->background == cell->background) return 0;
    *stored = *cell;
    return 1;
}

static int clamp(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

static void move_lines(int top, int bottom, int downward, int left, int count, int rightward, int pitch, int item,
    BYTE *base) {
    int first = downward > 0 ? bottom - 1 : top, last = downward > 0 ? top - 1 : bottom, step = downward > 0 ? -1 : 1;
    for (int line = first; line != last; line += step) {
        BYTE *source = base + (line * pitch + left) * item;
        BYTE *target = base + ((line + downward) * pitch + left + rightward) * item;
        memmove(target, source, (size_t)(count * item));
    }
}

void canvas_move(struct canvas *canvas, int origin_x, int origin_y, int cell_width, int cell_height, int top, int bottom,
    int left, int right, int downward, int rightward) {
    top = clamp(top, 0, canvas->rows);
    bottom = clamp(bottom, 0, canvas->rows);
    left = clamp(left, 0, canvas->columns);
    right = clamp(right, 0, canvas->columns);
    if (top + downward < 0 || bottom + downward > canvas->rows || left + rightward < 0 || right + rightward > canvas->columns) {
        canvas_forget(canvas);
        return;
    }
    if (bottom <= top || right <= left) return;
    int pixel_top = origin_y + top * cell_height, pixel_bottom = origin_y + bottom * cell_height;
    int pixel_left = origin_x + left * cell_width, pixel_count = (right - left) * cell_width;
    int pixel_down = downward * cell_height, pixel_right = rightward * cell_width;
    if (pixel_top + pixel_down < 0 || pixel_bottom + pixel_down > canvas->height || pixel_left + pixel_right < 0 ||
        pixel_left + pixel_right + pixel_count > canvas->width || pixel_left + pixel_count > canvas->width ||
        pixel_bottom > canvas->height) {
        canvas_forget(canvas);
        return;
    }
    move_lines(top, bottom, downward, left, right - left, rightward, canvas->columns, sizeof *canvas->cells,
        (BYTE *)canvas->cells);
    move_lines(pixel_top, pixel_bottom, pixel_down, pixel_left, pixel_count, pixel_right, canvas->pitch,
        sizeof *canvas->pixels, (BYTE *)canvas->pixels);
}
