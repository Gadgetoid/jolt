#pragma once
#include <windows.h>
#include "linux.h"

void tty_default_settings(struct linux_termios *settings);
void tty_input(char character);
void tty_type(const char *bytes, DWORD length);
int tty_line_pending(void);
