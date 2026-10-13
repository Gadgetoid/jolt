#pragma once
#include <windows.h>

int serial_port_named(const char *name);
int serial_port_of(const char *absolute);
LONG serial_open(int port);
LONG serial_read(int index, char *buffer, DWORD length, int nonblocking);
LONG serial_write(int index, const char *buffer, DWORD length, int nonblocking);
int serial_readiness(int index);
DWORD serial_pending(int index);
LONG serial_ioctl(int index, DWORD request, DWORD argument);
