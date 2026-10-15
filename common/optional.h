#pragma once
#include <windows.h>

void *jolt_optional(const WCHAR *name);
void *jolt_optional_in(const WCHAR *library, const WCHAR *name);
int jolt_crypt_random(BYTE *buffer, DWORD length);
