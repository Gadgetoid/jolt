#pragma once
#include <windows.h>

int script_load(const WCHAR *name);
int script_active(void);
void script_step(void);
