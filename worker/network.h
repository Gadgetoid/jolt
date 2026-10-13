#pragma once
#include <windows.h>

LONG network_read(int index, char *buffer, DWORD length);
LONG network_write(int index, const char *buffer, DWORD length);
int network_readiness(int index, int events);
DWORD network_pending(int index);
void network_release(int index);
LONG stream_shutdown(int index, DWORD how);
