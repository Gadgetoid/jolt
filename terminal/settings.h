#pragma once
#include "display.h"

void settings_load(struct display_settings *settings);
void settings_command(const char *command);
const struct display_settings *settings_current(void);
