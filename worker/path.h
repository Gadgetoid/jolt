#ifndef JOLT_PATH_H
#define JOLT_PATH_H

#include <stddef.h>

#define JOLT_PATH_CAPACITY 260

int jolt_resolve_path(const char *guest, char *relative, size_t capacity);
int jolt_absolute_path(const char *base, const char *guest, char *absolute, size_t capacity);

#endif
