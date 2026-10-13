#pragma once
#include <windows.h>

void keyboard_key_down(WPARAM key);
void keyboard_character(WPARAM character);
int keyboard_system_key(WPARAM key);
int keyboard_system_character(void);
void keyboard_start_log(void);
void keyboard_log(UINT message, WPARAM wparam, LPARAM lparam);
