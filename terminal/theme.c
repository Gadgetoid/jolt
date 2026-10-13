#include <string.h>
#include "theme.h"

#define SOLARIZED_BASE03 RGB(0x00, 0x2b, 0x36)
#define SOLARIZED_BASE02 RGB(0x07, 0x36, 0x42)
#define SOLARIZED_BASE01 RGB(0x58, 0x6e, 0x75)
#define SOLARIZED_BASE00 RGB(0x65, 0x7b, 0x83)
#define SOLARIZED_BASE0 RGB(0x83, 0x94, 0x96)
#define SOLARIZED_BASE1 RGB(0x93, 0xa1, 0xa1)
#define SOLARIZED_BASE2 RGB(0xee, 0xe8, 0xd5)
#define SOLARIZED_BASE3 RGB(0xfd, 0xf6, 0xe3)
#define SOLARIZED_YELLOW RGB(0xb5, 0x89, 0x00)
#define SOLARIZED_ORANGE RGB(0xcb, 0x4b, 0x16)
#define SOLARIZED_RED RGB(0xdc, 0x32, 0x2f)
#define SOLARIZED_MAGENTA RGB(0xd3, 0x36, 0x82)
#define SOLARIZED_VIOLET RGB(0x6c, 0x71, 0xc4)
#define SOLARIZED_BLUE RGB(0x26, 0x8b, 0xd2)
#define SOLARIZED_CYAN RGB(0x2a, 0xa1, 0x98)
#define SOLARIZED_GREEN RGB(0x85, 0x99, 0x00)

#define CLASSIC_ANSI { \
    RGB(0, 0, 0), RGB(170, 0, 0), RGB(0, 150, 0), RGB(170, 110, 0), \
    RGB(0, 0, 190), RGB(170, 0, 170), RGB(0, 150, 150), RGB(190, 190, 190), \
    RGB(90, 90, 90), RGB(240, 60, 60), RGB(40, 200, 40), RGB(220, 200, 40), \
    RGB(70, 70, 250), RGB(230, 70, 230), RGB(40, 210, 210), RGB(255, 255, 255) }

#define SOLARIZED_ANSI { \
    SOLARIZED_BASE02, SOLARIZED_RED, SOLARIZED_GREEN, SOLARIZED_YELLOW, \
    SOLARIZED_BLUE, SOLARIZED_MAGENTA, SOLARIZED_CYAN, SOLARIZED_BASE2, \
    SOLARIZED_BASE03, SOLARIZED_ORANGE, SOLARIZED_BASE01, SOLARIZED_BASE00, \
    SOLARIZED_BASE0, SOLARIZED_VIOLET, SOLARIZED_BASE1, SOLARIZED_BASE3 }

static const struct theme classic_light = { RGB(20, 20, 20), RGB(255, 255, 255), CLASSIC_ANSI };
static const struct theme classic_dark = { RGB(225, 225, 225), RGB(0, 0, 0), CLASSIC_ANSI };
static const struct theme solarized_light = { SOLARIZED_BASE00, SOLARIZED_BASE3, SOLARIZED_ANSI };
static const struct theme solarized_dark = { SOLARIZED_BASE0, SOLARIZED_BASE03, SOLARIZED_ANSI };

const struct theme *theme_lookup(enum theme_name name, int dark) {
    if (name == THEME_SOLARIZED) return dark ? &solarized_dark : &solarized_light;
    return dark ? &classic_dark : &classic_light;
}

int theme_parse(const char *text, enum theme_name *name) {
    if (!strcmp(text, "classic")) *name = THEME_CLASSIC;
    else if (!strcmp(text, "solarized")) *name = THEME_SOLARIZED;
    else return 0;
    return 1;
}

const char *theme_label(enum theme_name name) {
    return name == THEME_SOLARIZED ? "solarized" : "classic";
}
