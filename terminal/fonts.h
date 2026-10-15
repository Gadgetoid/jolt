#pragma once
#include <windows.h>

#define FONTS_FOLDER L"root\\usr\\share\\fonts\\"

int fonts_resolve(HDC dc, const char *face, WCHAR *family);
void fonts_release(void);
