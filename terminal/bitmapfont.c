#include <windows.h>
#include <string.h>
#include <wchar.h>
#include "fonts.h"
#include "bitmapfont.h"
#include "session.h"

#define FONT_EXTENSION L".jbf"
#define BOLD_SUFFIX L"-bold"
#define MAGIC "JBF1"
#define HEADER_SIZE 16
#define FACE_CAPACITY 32
#define FILE_LIMIT (256 * 1024)
#define MAXIMUM_WIDTH 16
#define EXPANSION_SLOTS 4
#define EXPANSION_PATTERNS 256
#define EXPANSION_WIDTH 8

struct expansion {
    WORD foreground, background;
    int used;
    WORD pixels[EXPANSION_PATTERNS][EXPANSION_WIDTH];
};

static struct expansion expansions[EXPANSION_SLOTS];
static int expansion_clock;

struct font_header {
    char magic[4];
    WORD width, height, columns, stride;
    DWORD count;
};

static int wide_face(const char *face, WCHAR *wide) {
    size_t length = strlen(face);
    if (!length || length >= FACE_CAPACITY) return 0;
    for (size_t index = 0; index <= length; index++) {
        char character = face[index];
        if (character >= 'A' && character <= 'Z') character = (char)(character - 'A' + 'a');
        if (character == '/' || character == '\\' || character == '*' || character == '?') return 0;
        wide[index] = (WCHAR)(unsigned char)character;
    }
    return 1;
}

static void font_path(WCHAR *path, const WCHAR *face, int height, int bold) {
    WCHAR name[MAX_PATH];
    wsprintfW(name, L"%s%s-%d%s%s", FONTS_FOLDER, face, height, bold ? BOLD_SUFFIX : L"", FONT_EXTENSION);
    session_path(path, name);
}

static void sort_heights(int *heights, int count) {
    for (int outer = 1; outer < count; outer++)
        for (int inner = outer; inner > 0 && heights[inner - 1] > heights[inner]; inner--) {
            int swap = heights[inner];
            heights[inner] = heights[inner - 1];
            heights[inner - 1] = swap;
        }
}

int bitmap_font_heights(const char *face, int *heights) {
    WCHAR wide[FACE_CAPACITY], name[MAX_PATH], pattern[MAX_PATH];
    if (!wide_face(face, wide)) return 0;
    wsprintfW(name, L"%s%s-*%s", FONTS_FOLDER, wide, FONT_EXTENSION);
    session_path(pattern, name);
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return 0;
    int count = 0;
    size_t prefix = wcslen(wide) + 1;
    do {
        const WCHAR *cursor = found.cFileName + prefix;
        int height = 0;
        while (*cursor >= L'0' && *cursor <= L'9') height = height * 10 + (*cursor++ - L'0');
        if (height && !_wcsicmp(cursor, FONT_EXTENSION) && count < BITMAP_FONT_HEIGHTS) heights[count++] = height;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    sort_heights(heights, count);
    return count;
}

int bitmap_font_pick(const char *face, int requested) {
    int heights[BITMAP_FONT_HEIGHTS];
    int count = bitmap_font_heights(face, heights);
    if (!count) return 0;
    int chosen = heights[0];
    for (int index = 0; index < count; index++)
        if (heights[index] <= requested) chosen = heights[index];
    return chosen;
}

int bitmap_font_step(const char *face, int current, int direction) {
    int heights[BITMAP_FONT_HEIGHTS];
    int count = bitmap_font_heights(face, heights);
    int chosen = bitmap_font_pick(face, current);
    for (int index = 0; index < count; index++) {
        if (heights[index] != chosen) continue;
        int next = index + direction;
        return next >= 0 && next < count ? heights[next] : 0;
    }
    return 0;
}

static BYTE *read_file(const WCHAR *path, DWORD *size) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    DWORD length = GetFileSize(file, NULL), count = 0;
    BYTE *data = length > HEADER_SIZE && length <= FILE_LIMIT ? LocalAlloc(LMEM_FIXED, length) : NULL;
    int read = data && ReadFile(file, data, length, &count, NULL) && count == length;
    CloseHandle(file);
    if (read) {
        *size = length;
        return data;
    }
    if (data) LocalFree(data);
    return NULL;
}

static WORD *unpack_rows(const struct font_header *header, const BYTE *bits) {
    WORD *rows = LocalAlloc(LMEM_FIXED, header->count * header->height * sizeof(WORD));
    if (!rows) return NULL;
    for (DWORD glyph = 0; glyph < header->count; glyph++) {
        int left = (int)(glyph % header->columns) * header->width;
        int top = (int)(glyph / header->columns) * header->height;
        for (int y = 0; y < header->height; y++) {
            const BYTE *line = bits + (top + y) * header->stride;
            WORD value = 0;
            for (int x = 0; x < header->width; x++) {
                int bit = left + x;
                if (!(line[bit >> 3] & (0x80 >> (bit & 7)))) value |= (WORD)(0x8000 >> x);
            }
            rows[glyph * header->height + y] = value;
        }
    }
    return rows;
}

static int load_atlas(const WCHAR *path, struct bitmap_font *font, int slot) {
    DWORD size = 0;
    BYTE *data = read_file(path, &size);
    if (!data) return 0;
    struct font_header header;
    memcpy(&header, data, sizeof header);
    DWORD rows = (header.count + header.columns - 1) / header.columns * header.height;
    DWORD needed = HEADER_SIZE + header.count * sizeof(DWORD) + rows * header.stride;
    int valid = !memcmp(header.magic, MAGIC, sizeof header.magic) && header.columns && header.width &&
        header.width <= MAXIMUM_WIDTH && header.height &&
        header.stride == (header.columns * header.width + 15) / 16 * 2 && needed <= size;
    if (valid && slot == 0) {
        font->width = header.width;
        font->height = header.height;
        font->count = (int)header.count;
        font->codepoints = LocalAlloc(LMEM_FIXED, header.count * sizeof(DWORD));
        if (font->codepoints) memcpy(font->codepoints, data + HEADER_SIZE, header.count * sizeof(DWORD));
        else valid = 0;
    } else if (valid) {
        valid = font->width == header.width && font->height == header.height && font->count == (int)header.count;
    }
    if (valid) {
        font->rows[slot] = unpack_rows(&header, data + HEADER_SIZE + header.count * sizeof(DWORD));
        valid = font->rows[slot] != NULL;
    }
    LocalFree(data);
    return valid;
}

void bitmap_font_free(struct bitmap_font *font) {
    for (int slot = 0; slot < BITMAP_FONT_STYLES; slot++)
        if (font->rows[slot]) LocalFree(font->rows[slot]);
    if (font->codepoints) LocalFree(font->codepoints);
    memset(font, 0, sizeof *font);
}

int bitmap_font_load(const char *face, int height, struct bitmap_font *font) {
    WCHAR wide[FACE_CAPACITY], path[MAX_PATH];
    memset(font, 0, sizeof *font);
    if (!wide_face(face, wide)) return 0;
    font_path(path, wide, height, 0);
    if (!load_atlas(path, font, 0)) {
        bitmap_font_free(font);
        return 0;
    }
    font_path(path, wide, height, 1);
    load_atlas(path, font, 1);
    return 1;
}

int bitmap_font_index(const struct bitmap_font *font, WCHAR character) {
    int low = 0, high = font->count - 1;
    while (low <= high) {
        int middle = (low + high) / 2;
        DWORD value = font->codepoints[middle];
        if (value == character) return middle;
        if (value < character) low = middle + 1;
        else high = middle - 1;
    }
    return -1;
}

static const struct expansion *expansion_for(WORD foreground, WORD background) {
    struct expansion *oldest = &expansions[0];
    for (int slot = 0; slot < EXPANSION_SLOTS; slot++) {
        struct expansion *candidate = &expansions[slot];
        if (candidate->used && candidate->foreground == foreground && candidate->background == background) {
            candidate->used = ++expansion_clock;
            return candidate;
        }
        if (candidate->used < oldest->used) oldest = candidate;
    }
    for (int bits = 0; bits < EXPANSION_PATTERNS; bits++)
        for (int pixel = 0; pixel < EXPANSION_WIDTH; pixel++)
            oldest->pixels[bits][pixel] = bits & (0x80 >> pixel) ? foreground : background;
    oldest->foreground = foreground;
    oldest->background = background;
    oldest->used = ++expansion_clock;
    return oldest;
}

void bitmap_font_render(const struct bitmap_font *font, WORD *target, int pitch, int index, int bold, WORD foreground,
    WORD background, int underline) {
    const struct expansion *table = expansion_for(foreground, background);
    const WORD *glyph = NULL;
    if (index >= 0) glyph = (bold && font->rows[1] ? font->rows[1] : font->rows[0]) + index * font->height;
    int first = font->width < EXPANSION_WIDTH ? font->width : EXPANSION_WIDTH;
    int second = font->width - first;
    for (int y = 0; y < font->height; y++, target += pitch) {
        WORD bits = glyph ? glyph[y] : 0;
        if (underline && y == font->height - 1) bits = 0xffff;
        const WORD *high = table->pixels[bits >> 8], *low = table->pixels[bits & 0xff];
        for (int x = 0; x < first; x++) target[x] = high[x];
        for (int x = 0; x < second; x++) target[EXPANSION_WIDTH + x] = low[x];
    }
}
