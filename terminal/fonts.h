#pragma once
#include <windows.h>

int fonts_resolve(HDC dc, const char *face, WCHAR *family);
void fonts_release(void);
