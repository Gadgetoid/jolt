#pragma once
#include <windows.h>

enum procfs_kind { PROCFS_MISSING, PROCFS_DIRECTORY, PROCFS_FILE, PROCFS_EXECUTABLE, PROCFS_LINK };

int procfs_owns(const char *absolute);
enum procfs_kind procfs_lookup(const char *absolute);
int procfs_entry(const char *directory, DWORD position, char *name, BYTE *type);
LONG procfs_render(const char *absolute, char *buffer, DWORD capacity);
LONG procfs_readlink(const char *absolute, char *buffer, DWORD capacity);
