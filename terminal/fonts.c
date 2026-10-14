#include <windows.h>
#include <string.h>
#include <wchar.h>
#include "fonts.h"
#include "session.h"

#define FONTS_FOLDER L"root\\usr\\share\\fonts\\"
#define FONT_PATTERN L"*.ttf"
#define LOADED_LIMIT 16

struct family_search {
    const WCHAR *compact;
    WCHAR *family;
    int found;
};

static WCHAR loaded[LOADED_LIMIT][MAX_PATH];
static int loaded_count;

static void compact_name(const WCHAR *name, WCHAR *compact, size_t capacity) {
    size_t length = 0;
    for (; *name && length + 1 < capacity; name++)
        if (*name != L' ') compact[length++] = *name;
    compact[length] = 0;
}

static int already_loaded(const WCHAR *path) {
    for (int index = 0; index < loaded_count; index++)
        if (!_wcsicmp(loaded[index], path)) return 1;
    return 0;
}

static void load_family_files(const WCHAR *compact) {
    WCHAR folder[MAX_PATH], pattern[MAX_PATH], path[MAX_PATH];
    session_path(folder, FONTS_FOLDER);
    size_t prefix = wcslen(compact);
    if (wcslen(folder) + prefix + wcslen(FONT_PATTERN) >= MAX_PATH) return;
    wcscpy(pattern, folder);
    wcscat(pattern, compact);
    wcscat(pattern, FONT_PATTERN);
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        WCHAR next = found.cFileName[prefix];
        if (next != L'.' && next != L'-') continue;
        if (wcslen(folder) + wcslen(found.cFileName) >= MAX_PATH) continue;
        wcscpy(path, folder);
        wcscat(path, found.cFileName);
        if (already_loaded(path) || loaded_count == LOADED_LIMIT) continue;
        if (AddFontResourceW(path)) wcscpy(loaded[loaded_count++], path);
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

static int CALLBACK match_family(const LOGFONTW *font, const TEXTMETRICW *metrics, DWORD type, LPARAM context) {
    (void)metrics;
    (void)type;
    struct family_search *search = (struct family_search *)context;
    WCHAR compact[LF_FACESIZE];
    compact_name(font->lfFaceName, compact, LF_FACESIZE);
    if (_wcsicmp(compact, search->compact)) return 1;
    wcscpy(search->family, font->lfFaceName);
    search->found = 1;
    return 0;
}

int fonts_resolve(HDC dc, const char *face, WCHAR *family) {
    WCHAR wide[LF_FACESIZE], compact[LF_FACESIZE];
    size_t length = strlen(face);
    if (!length || length >= LF_FACESIZE) return 0;
    for (size_t index = 0; index <= length; index++) wide[index] = (WCHAR)(unsigned char)face[index];
    compact_name(wide, compact, LF_FACESIZE);
    load_family_files(compact);
    struct family_search search = { compact, family, 0 };
    EnumFontFamiliesW(dc, NULL, match_family, (LPARAM)&search);
    return search.found;
}

void fonts_release(void) {
    while (loaded_count) RemoveFontResourceW(loaded[--loaded_count]);
}
