#include <windows.h>
#include "tty.h"
#include "kernel.h"

void tty_input(char character) {
    jolt_tty_input(JOLT_CONSOLE, character);
}

void tty_type(const char *bytes, DWORD length) {
    for (DWORD index = 0; index < length; index++) tty_input(bytes[index]);
}
