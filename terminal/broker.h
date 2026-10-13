#pragma once
#include <windows.h>

int broker_load_image(void);
int broker_start(void);
void broker_monitor(void);
void broker_stop(void);
int broker_idle(DWORD quiet_interval);
