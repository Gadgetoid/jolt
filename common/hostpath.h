#pragma once
#include <windows.h>
#include <stddef.h>

int jolt_find_mount(const char *absolute, size_t *matched);
int jolt_is_mount_point(const char *absolute);
LONG jolt_host_path(const WCHAR *root, const char *absolute, WCHAR *path);
