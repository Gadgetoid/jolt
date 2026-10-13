#include <windows.h>
#include <string.h>
#include <jornada/sdk/wincrypt.h>
#include "random.h"
#include "worker.h"
#include "digest.h"

#ifndef PROV_RSA_FULL
#define PROV_RSA_FULL 1
#endif
#ifndef CRYPT_VERIFYCONTEXT
#define CRYPT_VERIFYCONTEXT 0xF0000000UL
#endif

static HCRYPTPROV provider;
static int provider_state;

static int secure_provider(void) {
    if (!provider_state)
        provider_state = CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) ? 1 : -1;
    return provider_state > 0;
}

void random_fill(BYTE *buffer, DWORD length) {
    if (secure_provider() && CryptGenRandom(provider, length, buffer)) return;
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
