#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "bitmapfont.h"
#include "session.h"

#define FONTS_FOLDER L"root\\usr\\share\\fonts\\"
#define FONT_EXTENSION L".jbf"
#define BOLD_SUFFIX L"-bold"
#define MAGIC "JBF1"
#define HEADER_SIZE 16
#define FACE_CAPACITY 32
#define FILE_LIMIT (256 * 1024)
#define RED_MASK 0xf800
#define GREEN_MASK 0x07e0
#define BLUE_MASK 0x001f

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

static int load_atlas(const WCHAR *path, struct bitmap_font *font, int slot) {
    DWORD size = 0;
    BYTE *data = read_file(path, &size);
    if (!data) return 0;
    struct font_header header;
    memcpy(&header, data, sizeof header);
    DWORD rows = (header.count + header.columns - 1) / header.columns * header.height;
    DWORD bits_size = rows * header.stride;
    DWORD needed = HEADER_SIZE + header.count * sizeof(DWORD) + bits_size;
    int valid = !memcmp(header.magic, MAGIC, sizeof header.magic) && header.columns && header.width && header.height &&
        header.stride == (header.columns * header.width + 15) / 16 * 2 && needed <= size;
    if (valid && slot == 0) {
        font->width = header.width;
        font->height = header.height;
        font->columns = header.columns;
        font->count = (int)header.count;
        font->stride = header.stride;
        font->codepoints = LocalAlloc(LMEM_FIXED, header.count * sizeof(DWORD));
        if (font->codepoints) memcpy(font->codepoints, data + HEADER_SIZE, header.count * sizeof(DWORD));
        else valid = 0;
    } else if (valid) {
        valid = font->width == header.width && font->height == header.height && font->count == (int)header.count &&
            font->columns == header.columns && font->stride == header.stride;
    }
    if (valid) {
        font->bits[slot] = LocalAlloc(LMEM_FIXED, bits_size);
        if (font->bits[slot]) memcpy(font->bits[slot], data + HEADER_SIZE + header.count * sizeof(DWORD), bits_size);
        else valid = 0;
    }
    LocalFree(data);
    return valid;
}

void bitmap_font_free(struct bitmap_font *font) {
    for (int slot = 0; slot < 2; slot++)
        if (font->bits[slot]) LocalFree(font->bits[slot]);
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

int pixel_row_create(struct pixel_row *row, int width, int height) {
    struct {
        BITMAPINFOHEADER header;
        DWORD masks[3];
    } info;
    memset(row, 0, sizeof *row);
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
    row->bitmap = CreateDIBSection(NULL, (BITMAPINFO *)&info, DIB_RGB_COLORS, &pixels, NULL, 0);
    row->dc = row->bitmap ? CreateCompatibleDC(NULL) : NULL;
    if (!row->bitmap || !row->dc || !pixels) {
        pixel_row_free(row);
        return 0;
    }
    SelectObject(row->dc, row->bitmap);
    row->pixels = pixels;
    row->width = width;
    row->height = height;
    row->pitch = (width * 2 + 3) / 4 * 2;
    return 1;
}

void pixel_row_free(struct pixel_row *row) {
    if (row->dc) DeleteDC(row->dc);
    if (row->bitmap) DeleteObject(row->bitmap);
    memset(row, 0, sizeof *row);
}

WORD pixel_colour(COLORREF colour) {
    return (WORD)(((GetRValue(colour) >> 3) << 11) | ((GetGValue(colour) >> 2) << 5) | (GetBValue(colour) >> 3));
}

void pixel_row_glyph(struct pixel_row *row, const struct bitmap_font *font, int x, int index, int bold, WORD foreground,
    WORD background, int underline) {
    const BYTE *bits = bold && font->bits[1] ? font->bits[1] : font->bits[0];
    int left = index >= 0 ? (index % font->columns) * font->width : 0;
    int top = index >= 0 ? (index / font->columns) * font->height : 0;
    for (int y = 0; y < font->height && y < row->height; y++) {
        WORD *target = row->pixels + y * row->pitch + x;
        const BYTE *source = bits + (top + y) * font->stride;
        int line = underline && y == font->height - 1;
        for (int column = 0; column < font->width && x + column < row->width; column++) {
            int bit = left + column;
            int ink = index >= 0 && !(source[bit >> 3] & (0x80 >> (bit & 7)));
            target[column] = ink || line ? foreground : background;
        }
    }
}
