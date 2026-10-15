#include <string.h>
#include "canvas.h"
#include "scrollback.h"

#define SCROLLBACK_LINES 400

struct stored_cell {
    WCHAR character;
    BYTE style;
    WORD foreground;
    WORD background;
};

static struct stored_cell *lines;
static int first, count, width;

static WCHAR stored_character(WCHAR character) {
    return character >= L' ' ? character : L' ';
}

int scrollback_init(int columns) {
    width = columns;
    lines = LocalAlloc(LMEM_FIXED, (UINT)(SCROLLBACK_LINES * width) * sizeof *lines);
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
    return lines + ((first + line) % SCROLLBACK_LINES) * width;
}

void scrollback_push(int columns, const struct styled_cell *cells) {
    if (!lines) return;
    if (count == SCROLLBACK_LINES) first = (first + 1) % SCROLLBACK_LINES;
    else count++;
    struct stored_cell *line = line_at(count - 1);
    for (int column = 0; column < width; column++) {
        struct stored_cell *stored = &line[column];
        if (column >= columns) {
            stored->character = ' ';
            stored->style = STYLE_DEFAULT_FOREGROUND | STYLE_DEFAULT_BACKGROUND;
            stored->foreground = stored->background = 0;
            continue;
        }
        stored->character = stored_character(cells[column].character);
        stored->style = cells[column].style;
        stored->foreground = canvas_colour(cells[column].foreground);
        stored->background = canvas_colour(cells[column].background);
    }
}

void scrollback_cell(int line, int column, struct styled_cell *cell) {
    memset(cell, 0, sizeof *cell);
    cell->character = ' ';
    cell->style = STYLE_DEFAULT_FOREGROUND | STYLE_DEFAULT_BACKGROUND;
    if (!lines || line < 0 || line >= count || column < 0 || column >= width) return;
    const struct stored_cell *stored = &line_at(line)[column];
    cell->character = stored->character;
    cell->style = stored->style;
    cell->foreground = canvas_rgb(stored->foreground);
    cell->background = canvas_rgb(stored->background);
}

int scrollback_pop(int columns, struct styled_cell *cells) {
    if (!lines || !count) return 0;
    for (int column = 0; column < columns; column++) scrollback_cell(count - 1, column, &cells[column]);
    count--;
    return 1;
}
