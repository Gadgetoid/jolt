#include <stddef.h>
#include "boxdraw.h"

#define FIRST_LINE 0x2500
#define FIRST_BLOCK 0x2580
#define LAST_BLOCK 0x259f
#define EIGHTHS 8
#define STROKE_DIVISOR 8
#define DOUBLE_STROKES 3

struct cell {
    uint16_t *target;
    int pitch, width, height, left, top;
    uint16_t foreground, background;
    int light, heavy;
};

struct span {
    int start, end;
};

struct eighths {
    char left, top, right, bottom;
};

static const char *const line_arms[] = {
    "l-l-", "h-h-", "-l-l", "-h-h", NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, "--ll", "--hl", "--lh", "--hh",
    "l--l", "h--l", "l--h", "h--h", "-ll-", "-lh-", "-hl-", "-hh-",
    "ll--", "hl--", "lh--", "hh--", "-lll", "-lhl", "-hll", "-llh",
    "-hlh", "-hhl", "-lhh", "-hhh", "ll-l", "hl-l", "lh-l", "ll-h",
    "lh-h", "hh-l", "hl-h", "hh-h", "l-ll", "h-ll", "l-hl", "h-hl",
    "l-lh", "h-lh", "l-hh", "h-hh", "lll-", "hll-", "llh-", "hlh-",
    "lhl-", "hhl-", "lhh-", "hhh-", "llll", "hlll", "llhl", "hlhl",
    "lhll", "lllh", "lhlh", "hhll", "lhhl", "hllh", "llhh", "hhhl",
    "hlhh", "hhlh", "lhhh", "hhhh", NULL, NULL, NULL, NULL,
    "d-d-", "-d-d", "--dl", "--ld", "--dd", "d--l", "l--d", "d--d",
    "-ld-", "-dl-", "-dd-", "dl--", "ld--", "dd--", "-ldl", "-dld",
    "-ddd", "dl-l", "ld-d", "dd-d", "d-dl", "l-ld", "d-dd", "dld-",
    "ldl-", "ddd-", "dldl", "ldld", "dddd", NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, "l---", "-l--", "--l-", "---l",
    "h---", "-h--", "--h-", "---h", "l-h-", "-l-h", "h-l-", "-h-l",
};

static const struct eighths block_rectangles[] = {
    { 0, 0, 8, 4 }, { 0, 7, 8, 8 }, { 0, 6, 8, 8 }, { 0, 5, 8, 8 }, { 0, 4, 8, 8 }, { 0, 3, 8, 8 },
    { 0, 2, 8, 8 }, { 0, 1, 8, 8 }, { 0, 0, 8, 8 }, { 0, 0, 7, 8 }, { 0, 0, 6, 8 }, { 0, 0, 5, 8 },
    { 0, 0, 4, 8 }, { 0, 0, 3, 8 }, { 0, 0, 2, 8 }, { 0, 0, 1, 8 }, { 4, 0, 8, 8 },
};

enum quadrant { UPPER_LEFT = 1, UPPER_RIGHT = 2, LOWER_LEFT = 4, LOWER_RIGHT = 8 };

static const char quadrant_masks[] = {
    LOWER_LEFT, LOWER_RIGHT, UPPER_LEFT, UPPER_LEFT | LOWER_LEFT | LOWER_RIGHT, UPPER_LEFT | LOWER_RIGHT,
    UPPER_LEFT | UPPER_RIGHT | LOWER_LEFT, UPPER_LEFT | UPPER_RIGHT | LOWER_RIGHT, UPPER_RIGHT,
    UPPER_RIGHT | LOWER_LEFT, UPPER_RIGHT | LOWER_LEFT | LOWER_RIGHT,
};

static int larger(int first, int second) {
    return first > second ? first : second;
}

static int smaller(int first, int second) {
    return first < second ? first : second;
}

static void paint(const struct cell *cell, int left, int top, int right, int bottom, uint16_t colour) {
    left = larger(left, 0);
    top = larger(top, 0);
    right = smaller(right, cell->width);
    bottom = smaller(bottom, cell->height);
    for (int y = top; y < bottom; y++) {
        uint16_t *line = cell->target + y * cell->pitch;
        for (int x = left; x < right; x++) line[x] = colour;
    }
}

static void plot(const struct cell *cell, int x, int y) {
    cell->target[y * cell->pitch + x] = cell->foreground;
}

static struct span centred(int size, int thickness) {
    struct span span;
    span.start = (size - thickness) / 2;
    span.end = span.start + thickness;
    return span;
}

static int stroke_thickness(const struct cell *cell, char stroke) {
    switch (stroke) {
    case 'l': return cell->light;
    case 'h': return cell->heavy;
    case 'd': return cell->light * DOUBLE_STROKES;
    default: return 0;
    }
}

static int is_double(char first, char second) {
    return first == 'd' || second == 'd';
}

static int is_present(char first, char second) {
    return first != '-' || second != '-';
}

static void draw_lines(const struct cell *cell, const char *arms) {
    char left = arms[0], up = arms[1], right = arms[2], down = arms[3];
    int light = cell->light;
    int vertical = larger(stroke_thickness(cell, up), stroke_thickness(cell, down));
    int horizontal = larger(stroke_thickness(cell, left), stroke_thickness(cell, right));
    struct span columns = centred(cell->width, vertical ? vertical : horizontal);
    struct span rows = centred(cell->height, horizontal ? horizontal : vertical);
    int vertical_through_double = up == 'd' && down == 'd';
    int horizontal_through_double = left == 'd' && right == 'd';

    if (left != '-') {
        struct span band = centred(cell->height, stroke_thickness(cell, left));
        int end = vertical_through_double && left != 'd' && right == '-' ? columns.start + light : columns.end;
        paint(cell, 0, band.start, end, band.end, cell->foreground);
    }
    if (right != '-') {
        struct span band = centred(cell->height, stroke_thickness(cell, right));
        int start = vertical_through_double && right != 'd' && left == '-' ? columns.end - light : columns.start;
        paint(cell, start, band.start, cell->width, band.end, cell->foreground);
    }
    if (up != '-') {
        struct span band = centred(cell->width, stroke_thickness(cell, up));
        int end = horizontal_through_double && up != 'd' && down == '-' ? rows.start + light : rows.end;
        paint(cell, band.start, 0, band.end, end, cell->foreground);
    }
    if (down != '-') {
        struct span band = centred(cell->width, stroke_thickness(cell, down));
        int start = horizontal_through_double && down != 'd' && up == '-' ? rows.end - light : rows.start;
        paint(cell, band.start, start, band.end, cell->height, cell->foreground);
    }

    int vertical_double = is_double(up, down), vertical_present = is_present(up, down);
    int horizontal_double = is_double(left, right), horizontal_present = is_present(left, right);
    int gap_top = rows.start + light, gap_left = columns.start + light;
    if (left == 'd') {
        int end = vertical_double ? columns.start + 2 * light : vertical_present ? columns.start : cell->width;
        paint(cell, 0, gap_top, end, gap_top + light, cell->background);
    }
    if (right == 'd') {
        int start = vertical_double ? columns.start + light : vertical_present ? columns.end : 0;
        paint(cell, start, gap_top, cell->width, gap_top + light, cell->background);
    }
    if (up == 'd') {
        int end = horizontal_double ? rows.start + 2 * light : horizontal_present ? rows.start : cell->height;
        paint(cell, gap_left, 0, gap_left + light, end, cell->background);
    }
    if (down == 'd') {
        int start = horizontal_double ? rows.start + light : horizontal_present ? rows.end : 0;
        paint(cell, gap_left, start, gap_left + light, cell->height, cell->background);
    }
}

static void draw_dashes(const struct cell *cell, int thickness, int count, int vertical) {
    int length = vertical ? cell->height : cell->width;
    count = larger(smaller(count, length / 2), 1);
    struct span band = centred(vertical ? cell->width : cell->height, thickness);
    for (int index = 0; index < count; index++) {
        int start = length * index / count, end = length * (index + 1) / count;
        int gap = larger((end - start) / 4, 1);
        int dash_start = start + gap / 2, dash_end = end - (gap - gap / 2);
        if (vertical) paint(cell, band.start, dash_start, band.end, dash_end, cell->foreground);
        else paint(cell, dash_start, band.start, dash_end, band.end, cell->foreground);
    }
}

static void draw_arc(const struct cell *cell, int rightward, int downward) {
    int thickness = cell->light;
    struct span columns = centred(cell->width, thickness), rows = centred(cell->height, thickness);
    int centre_x = columns.start * 2 + thickness, centre_y = rows.start * 2 + thickness;
    int reach_x = rightward > 0 ? cell->width * 2 - centre_x : centre_x;
    int reach_y = downward > 0 ? cell->height * 2 - centre_y : centre_y;
    int radius = smaller(reach_x, reach_y);
    int pivot_x = centre_x + rightward * radius, pivot_y = centre_y + downward * radius;
    int inner = radius > thickness ? (radius - thickness) * (radius - thickness) : 0;
    int outer = (radius + thickness) * (radius + thickness);
    for (int y = 0; y < cell->height; y++) {
        for (int x = 0; x < cell->width; x++) {
            int offset_x = x * 2 + 1 - pivot_x, offset_y = y * 2 + 1 - pivot_y;
            int lit;
            if (offset_x * rightward > 0) lit = y >= rows.start && y < rows.end;
            else if (offset_y * downward > 0) lit = x >= columns.start && x < columns.end;
            else {
                int distance = offset_x * offset_x + offset_y * offset_y;
                lit = distance >= inner && distance < outer;
            }
            if (lit) plot(cell, x, y);
        }
    }
}

static void draw_diagonals(const struct cell *cell, int rising, int falling) {
    int width = cell->width, height = cell->height;
    int limit = cell->light * cell->light * (width * width + height * height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int along_x = x * 2 + 1, along_y = y * 2 + 1;
            int rise = height * along_x + width * along_y - 2 * width * height;
            int fall = height * along_x - width * along_y;
            if ((rising && rise * rise <= limit) || (falling && fall * fall <= limit)) plot(cell, x, y);
        }
    }
}

static int eighth(int size, int count) {
    return (size * count + EIGHTHS / 2) / EIGHTHS;
}

static void draw_eighths(const struct cell *cell, int left, int top, int right, int bottom) {
    paint(cell, eighth(cell->width, left), eighth(cell->height, top), eighth(cell->width, right),
        eighth(cell->height, bottom), cell->foreground);
}

static void draw_quadrants(const struct cell *cell, int mask) {
    int middle_x = eighth(cell->width, EIGHTHS / 2), middle_y = eighth(cell->height, EIGHTHS / 2);
    if (mask & UPPER_LEFT) paint(cell, 0, 0, middle_x, middle_y, cell->foreground);
    if (mask & UPPER_RIGHT) paint(cell, middle_x, 0, cell->width, middle_y, cell->foreground);
    if (mask & LOWER_LEFT) paint(cell, 0, middle_y, middle_x, cell->height, cell->foreground);
    if (mask & LOWER_RIGHT) paint(cell, middle_x, middle_y, cell->width, cell->height, cell->foreground);
}

static void draw_shade(const struct cell *cell, int level) {
    for (int y = 0; y < cell->height; y++) {
        for (int x = 0; x < cell->width; x++) {
            int absolute_x = cell->left + x, absolute_y = cell->top + y;
            int sparse = (absolute_x + 2 * absolute_y) % 4 == 0;
            int lit = level == 1 ? sparse : level == 2 ? (absolute_x + absolute_y) % 2 == 0 : !sparse;
            if (lit) plot(cell, x, y);
        }
    }
}

static void draw_line_glyph(const struct cell *cell, uint32_t character) {
    switch (character) {
    case 0x2504: draw_dashes(cell, cell->light, 3, 0); return;
    case 0x2505: draw_dashes(cell, cell->heavy, 3, 0); return;
    case 0x2506: draw_dashes(cell, cell->light, 3, 1); return;
    case 0x2507: draw_dashes(cell, cell->heavy, 3, 1); return;
    case 0x2508: draw_dashes(cell, cell->light, 4, 0); return;
    case 0x2509: draw_dashes(cell, cell->heavy, 4, 0); return;
    case 0x250a: draw_dashes(cell, cell->light, 4, 1); return;
    case 0x250b: draw_dashes(cell, cell->heavy, 4, 1); return;
    case 0x254c: draw_dashes(cell, cell->light, 2, 0); return;
    case 0x254d: draw_dashes(cell, cell->heavy, 2, 0); return;
    case 0x254e: draw_dashes(cell, cell->light, 2, 1); return;
    case 0x254f: draw_dashes(cell, cell->heavy, 2, 1); return;
    case 0x256d: draw_arc(cell, 1, 1); return;
    case 0x256e: draw_arc(cell, -1, 1); return;
    case 0x256f: draw_arc(cell, -1, -1); return;
    case 0x2570: draw_arc(cell, 1, -1); return;
    case 0x2571: draw_diagonals(cell, 1, 0); return;
    case 0x2572: draw_diagonals(cell, 0, 1); return;
    case 0x2573: draw_diagonals(cell, 1, 1); return;
    default: draw_lines(cell, line_arms[character - FIRST_LINE]); return;
    }
}

static void draw_block_glyph(const struct cell *cell, uint32_t character) {
    unsigned index = character - FIRST_BLOCK;
    if (index < sizeof block_rectangles / sizeof *block_rectangles) {
        const struct eighths *rectangle = &block_rectangles[index];
        draw_eighths(cell, rectangle->left, rectangle->top, rectangle->right, rectangle->bottom);
        return;
    }
    switch (character) {
    case 0x2591: draw_shade(cell, 1); return;
    case 0x2592: draw_shade(cell, 2); return;
    case 0x2593: draw_shade(cell, 3); return;
    case 0x2594: draw_eighths(cell, 0, 0, EIGHTHS, 1); return;
    case 0x2595: draw_eighths(cell, EIGHTHS - 1, 0, EIGHTHS, EIGHTHS); return;
    default: draw_quadrants(cell, quadrant_masks[character - 0x2596]); return;
    }
}

int boxdraw_covers(uint32_t character) {
    return character >= FIRST_LINE && character <= LAST_BLOCK;
}

void boxdraw_render(uint16_t *target, int pitch, int width, int height, int left, int top, uint32_t character, int bold,
    uint16_t foreground, uint16_t background, int underline) {
    struct cell cell = { target, pitch, width, height, left, top, foreground, background, 0, 0 };
    cell.light = larger(width / STROKE_DIVISOR, 1) + (bold ? 1 : 0);
    int room = smaller(width, height) - 2;
    if (cell.light * DOUBLE_STROKES > room) cell.light = larger(room / DOUBLE_STROKES, 1);
    cell.heavy = cell.light * 2;
    paint(&cell, 0, 0, width, height, background);
    if (character < FIRST_BLOCK) draw_line_glyph(&cell, character);
    else draw_block_glyph(&cell, character);
    if (underline) paint(&cell, 0, height - 1, width, height, foreground);
}
