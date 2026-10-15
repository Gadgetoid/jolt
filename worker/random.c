#include <windows.h>
#include <string.h>
#include "random.h"
#include "worker.h"
#include "digest.h"
#include "optional.h"

void random_fill(BYTE *buffer, DWORD length) {
    if (jolt_crypt_random(buffer, length)) return;
    for (DWORD index = 0; index < length; index++) buffer[index] = (BYTE)(next_random() >> 16);
}

struct fast_seed {
    BYTE entropy[JOLT_ENTROPY_SIZE];
    LONG pid;
    DWORD tick;
    DWORD counter;
};

void random_fill_fast(BYTE *buffer, DWORD length) {
    static DWORD counter;
    struct fast_seed seed;
    BYTE digest[JOLT_DIGEST_SIZE];
    memcpy(seed.entropy, jolt.kernel->entropy, sizeof seed.entropy);
    seed.pid = self->pid;
    seed.tick = GetTickCount();
    while (length) {
        seed.counter = counter++;
        jolt_sha256((const BYTE *)&seed, sizeof seed, digest);
        DWORD count = length < JOLT_DIGEST_SIZE ? length : JOLT_DIGEST_SIZE;
        memcpy(buffer, digest, count);
        buffer += count;
        length -= count;
    }
}
