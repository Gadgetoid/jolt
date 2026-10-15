#ifndef JOLT_PATH_H
#define JOLT_PATH_H

#include <stddef.h>

int jolt_absolute_path(const char *base, const char *guest, char *absolute, size_t capacity);

#endif
