#pragma once
#include <windows.h>

DWORD jolt_utf8_from_wide(const WCHAR *wide, char *text, DWORD capacity);
DWORD jolt_wide_from_utf8(const char *text, DWORD length, WCHAR *wide, DWORD capacity);
