#pragma once
#include <windows.h>
#include "display.h"

int hotkeys_engaged(const struct display_settings *settings);
int hotkeys_press(WPARAM key);
