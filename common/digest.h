#pragma once
#include <stddef.h>

void jolt_sha256(const unsigned char *data, size_t length, unsigned char digest[32]);
