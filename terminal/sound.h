#pragma once
#include <windows.h>

void sound_start(void);
void sound_stop(void);
void sound_bell(void);
void sound_tone(DWORD frequency, DWORD milliseconds, int slot);
