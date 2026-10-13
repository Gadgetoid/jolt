#pragma once
#include <windows.h>

#define JOLT_EVENTFD_SEMAPHORE 0x80000000UL

LONG eventfd_read(int index, char *buffer, DWORD length);
LONG eventfd_write(int index, const char *buffer, DWORD length);
int eventfd_readiness(int index);
