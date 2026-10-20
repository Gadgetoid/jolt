#include "boxdraw.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MARGIN 2
#define LARGEST 48
#define PITCH (LARGEST + 2 * MARGIN)
#define FOREGROUND 0xffff
#define BACKGROUND 0x0000
#define GUARD 0x1234
#define FIRST 0x2500
#define LAST 0x259f

enum edge { LEFT, TOP, RIGHT, BOTTOM, EDGES };

struct glyph {
    uint16_t pixels[PITCH * PITCH];
    int width, height;
};

static const uint32_t horizontal_references[] = { 0x2500, 0x2501, 0x2550 };
static const uint32_t vertical_references[] = { 0x2502, 0x2503, 0x2551 };

static uint16_t *origin(struct glyph *glyph) {
    return glyph->pixels + MARGIN * PITCH + MARGIN;
}

static void render(struct glyph *glyph, uint32_t character, int width, int height, int bold) {
    for (int index = 0; index < PITCH * PITCH; index++) glyph->pixels[index] = GUARD;
    glyph->width = width;
    glyph->height = height;
    boxdraw_render(origin(glyph), PITCH, width, height, 0, 0, character, bold, FOREGROUND, BACKGROUND, 0);
}

static int inside(const struct glyph *glyph, int x, int y) {
    return x >= MARGIN && y >= MARGIN && x < MARGIN + glyph->width && y < MARGIN + glyph->height;
}

static int edge_length(const struct glyph *glyph, enum edge edge) {
    return edge == LEFT || edge == RIGHT ? glyph->height : glyph->width;
}

static uint16_t edge_pixel(struct glyph *glyph, enum edge edge, int index) {
    const uint16_t *pixels = origin(glyph);
    switch (edge) {
    case LEFT: return pixels[index * PITCH];
    case RIGHT: return pixels[index * PITCH + glyph->width - 1];
    case TOP: return pixels[index];
    default: return pixels[(glyph->height - 1) * PITCH + index];
    }
}

static int edge_empty(struct glyph *glyph, enum edge edge) {
    for (int index = 0; index < edge_length(glyph, edge); index++)
        if (edge_pixel(glyph, edge, index) != BACKGROUND) return 0;
    return 1;
}

static int edges_match(struct glyph *glyph, struct glyph *reference, enum edge edge) {
    for (int index = 0; index < edge_length(glyph, edge); index++)
        if (edge_pixel(glyph, edge, index) != edge_pixel(reference, edge, index)) return 0;
    return 1;
}

static int edge_joins(struct glyph *glyph, enum edge edge, int width, int height, int bold) {
    if (edge_empty(glyph, edge)) return 1;
    const uint32_t *references = edge == LEFT || edge == RIGHT ? horizontal_references : vertical_references;
    struct glyph reference;
    for (int index = 0; index < 3; index++) {
        render(&reference, references[index], width, height, bold);
        if (edges_match(glyph, &reference, edge)) return 1;
    }
    return 0;
}

static int is_line(uint32_t character) {
    return character < 0x2580 && (character < 0x2571 || character > 0x2573);
}

static void check_glyph(uint32_t character, int width, int height, int bold) {
    struct glyph glyph;
    render(&glyph, character, width, height, bold);
    int lit = 0;
    for (int y = 0; y < PITCH; y++) {
        for (int x = 0; x < PITCH; x++) {
            uint16_t pixel = glyph.pixels[y * PITCH + x];
            if (!inside(&glyph, x, y)) assert(pixel == GUARD);
            else lit |= pixel == FOREGROUND;
        }
    }
    assert(lit);
    if (!is_line(character)) return;
    for (int edge = LEFT; edge < EDGES; edge++) {
        if (!edge_joins(&glyph, (enum edge)edge, width, height, bold)) {
            printf("FAIL: U+%04X %dx%d bold %d edge %d does not meet its neighbour\n", (unsigned)character, width,
                height, bold, edge);
            assert(0);
        }
    }
}

int main(void) {
    for (int bold = 0; bold <= 1; bold++)
        for (int width = 5; width <= 24; width++)
            for (int height = 8; height <= LARGEST; height++)
                for (uint32_t character = FIRST; character <= LAST; character++) check_glyph(character, width, height, bold);
    assert(!boxdraw_covers(0x24ff) && boxdraw_covers(FIRST) && boxdraw_covers(LAST) && !boxdraw_covers(LAST + 1));
    puts("PASS: box drawing and block glyphs fill their cells and meet at every edge");
    return 0;
}
