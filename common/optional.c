#include <windows.h>
#include "optional.h"

void *jolt_optional_in(const WCHAR *library, const WCHAR *name) {
    HMODULE module = GetModuleHandleW(library);
    if (!module) module = LoadLibraryW(library);
    return module ? (void *)GetProcAddressW(module, name) : NULL;
}

void *jolt_optional(const WCHAR *name) {
    static HMODULE coredll;
    if (!coredll) coredll = GetModuleHandleW(L"coredll.dll");
    return coredll ? (void *)GetProcAddressW(coredll, name) : NULL;
}

#define CRYPT_PROVIDER_RSA_FULL 1
#define CRYPT_VERIFY_CONTEXT 0xF0000000UL

typedef BOOL (WINAPI *crypt_acquire_function)(DWORD *, LPCWSTR, LPCWSTR, DWORD, DWORD);
typedef BOOL (WINAPI *crypt_random_function)(DWORD, DWORD, BYTE *);

int jolt_crypt_random(BYTE *buffer, DWORD length) {
    static DWORD provider;
    static int state;
    static crypt_random_function generate;
    if (!state) {
        crypt_acquire_function acquire = (crypt_acquire_function)jolt_optional(L"CryptAcquireContextW");
        generate = (crypt_random_function)jolt_optional(L"CryptGenRandom");
        int ready = acquire && generate &&
            acquire(&provider, NULL, NULL, CRYPT_PROVIDER_RSA_FULL, CRYPT_VERIFY_CONTEXT);
        state = ready ? 1 : -1;
    }
    return state > 0 && generate(provider, length, buffer);
}
