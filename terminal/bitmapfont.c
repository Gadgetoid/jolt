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
    DWORD needed = HEADER_SIZE + header.count * sizeof(DWORD) + rows * header.stride;
    int valid = !memcmp(header.magic, MAGIC, sizeof header.magic) && header.columns && header.width && header.height &&
        header.stride == (header.columns * header.width + 15) / 16 * 2 && needed <= size;
    if (valid && slot == 0) {
        font->width = header.width;
        font->height = header.height;
        font->columns = header.columns;
        font->count = (int)header.count;
        font->codepoints = LocalAlloc(LMEM_FIXED, header.count * sizeof(DWORD));
        if (font->codepoints) memcpy(font->codepoints, data + HEADER_SIZE, header.count * sizeof(DWORD));
        else valid = 0;
    } else if (valid) {
        valid = font->width == header.width && font->height == header.height && font->count == (int)header.count &&
            font->columns == header.columns;
    }
    if (valid) {
        const BYTE *bits = data + HEADER_SIZE + header.count * sizeof(DWORD);
        font->atlas[slot] = CreateBitmap(header.columns * header.width, (int)rows, 1, 1, bits);
        font->dc[slot] = CreateCompatibleDC(NULL);
        valid = font->atlas[slot] && font->dc[slot];
        if (valid) SelectObject(font->dc[slot], font->atlas[slot]);
    }
    if (!valid && slot) {
        if (font->dc[slot]) DeleteDC(font->dc[slot]);
        if (font->atlas[slot]) DeleteObject(font->atlas[slot]);
        font->dc[slot] = NULL;
        font->atlas[slot] = NULL;
    }
    LocalFree(data);
    return valid;
}

void bitmap_font_free(struct bitmap_font *font) {
    for (int slot = 0; slot < 2; slot++) {
        if (font->dc[slot]) DeleteDC(font->dc[slot]);
        if (font->atlas[slot]) DeleteObject(font->atlas[slot]);
    }
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

void bitmap_font_draw(HDC dc, const struct bitmap_font *font, int bold, int index, int x, int y) {
    HDC source = bold && font->dc[1] ? font->dc[1] : font->dc[0];
    BitBlt(dc, x, y, font->width, font->height, source, (index % font->columns) * font->width,
        (index / font->columns) * font->height, SRCCOPY);
}
