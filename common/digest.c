#include "digest.h"
#include "sha256.h"

void jolt_sha256(const unsigned char *data, size_t length, unsigned char digest[32]) {
    SHA256_CTX context;
    sha256_init(&context);
    sha256_update(&context, data, length);
    sha256_final(&context, digest);
}
