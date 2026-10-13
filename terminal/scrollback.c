#include <string.h>
#include "scrollback.h"

#define SCROLLBACK_LINES 400
#define SCROLLBACK_COLUMNS 128

struct stored_cell {
    BYTE character;
    BYTE style;
    WORD foreground;
    WORD background;
};

static struct stored_cell *lines;
static int first, count;

static WORD pack_colour(COLORREF colour) {
    return (WORD)(((GetRValue(colour) >> 3) << 11) | ((GetGValue(colour) >> 2) << 5) | (GetBValue(colour) >> 3));
}

static COLORREF unpack_colour(WORD colour) {
    BYTE red = (BYTE)((colour >> 11) << 3), green = (BYTE)(((colour >> 5) & 63) << 2), blue = (BYTE)((colour & 31) << 3);
    return RGB(red | red >> 5, green | green >> 6, blue | blue >> 5);
}

static BYTE narrow_character(WCHAR character) {
    if (character >= 32 && character < 256) return (BYTE)character;
    return character ? '?' : ' ';
}

int scrollback_init(void) {
    lines = LocalAlloc(LMEM_FIXED, SCROLLBACK_LINES * SCROLLBACK_COLUMNS * sizeof *lines);
    first = count = 0;
    return lines != NULL;
}

void scrollback_free(void) {
    if (lines) LocalFree(lines);
    lines = NULL;
}

int scrollback_count(void) {
    return lines ? count : 0;
}

static struct stored_cell *line_at(int line) {
    return lines + ((first + line) % SCROLLBACK_LINES) * SCROLLBACK_COLUMNS;
}

void scrollback_push(int columns, const struct styled_cell *cells) {
    if (!lines) return;
    if (count == SCROLLBACK_LINES) first = (first + 1) % SCROLLBACK_LINES;
    else count++;
    struct stored_cell *line = line_at(count - 1);
    for (int column = 0; column < SCROLLBACK_COLUMNS; column++) {
        struct stored_cell *stored = &line[column];
        if (column >= columns) {
            stored->character = ' ';
            stored->style = STYLE_DEFAULT_FOREGROUND | STYLE_DEFAULT_BACKGROUND;
            stored->foreground = stored->background = 0;
            continue;
        }
        stored->character = narrow_character(cells[column].character);
        stored->style = cells[column].style;
        stored->foreground = pack_colour(cells[column].foreground);
        stored->background = pack_colour(cells[column].background);
    }
}

void scrollback_cell(int line, int column, struct styled_cell *cell) {
    memset(cell, 0, sizeof *cell);
    cell->character = ' ';
    cell->style = STYLE_DEFAULT_FOREGROUND | STYLE_DEFAULT_BACKGROUND;
    if (!lines || line < 0 || line >= count || column < 0 || column >= SCROLLBACK_COLUMNS) return;
    const struct stored_cell *stored = &line_at(line)[column];
    cell->character = stored->character;
    cell->style = stored->style;
    cell->foreground = unpack_colour(stored->foreground);
    cell->background = unpack_colour(stored->background);
}

int scrollback_pop(int columns, struct styled_cell *cells) {
    if (!lines || !count) return 0;
    for (int column = 0; column < columns; column++) scrollback_cell(count - 1, column, &cells[column]);
    count--;
    return 1;
}
