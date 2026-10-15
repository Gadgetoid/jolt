#include <windows.h>
#include "optional.h"

#define CRYPT_PROVIDER_RSA_FULL 1
#define CRYPT_VERIFY_CONTEXT 0xF0000000UL
#define CORE_LIBRARY L"coredll.dll"

struct optional_function jolt_virtual_copy = { NULL, L"VirtualCopy", 0, NULL };
struct optional_function jolt_all_keys = { NULL, L"AllKeys", 0, NULL };
struct optional_function jolt_ws_control = { L"winsock.dll", L"WsControl", 0, NULL };
struct optional_function jolt_crypt_acquire = { NULL, L"CryptAcquireContextW", 0, NULL };
struct optional_function jolt_crypt_generate = { NULL, L"CryptGenRandom", 0, NULL };
struct optional_function jolt_create_shortcut = { NULL, L"SHCreateShortcut", 0, NULL };
struct optional_function jolt_load_bitmap = { NULL, L"SHLoadDIBitmap", 0, NULL };
struct optional_function jolt_idle_reset = { NULL, L"SystemIdleTimerReset", 0, NULL };
struct optional_function jolt_clipboard_read = { NULL, L"GetClipboardDataAlloc", 0, NULL };
struct optional_function jolt_store_information = { NULL, L"GetStoreInformation", 0, NULL };

struct optional_function *const jolt_optional_functions[] = {
    &jolt_virtual_copy, &jolt_all_keys, &jolt_ws_control, &jolt_crypt_acquire, &jolt_crypt_generate,
    &jolt_create_shortcut, &jolt_load_bitmap, &jolt_idle_reset, &jolt_clipboard_read, &jolt_store_information,
};
const int jolt_optional_count = sizeof jolt_optional_functions / sizeof jolt_optional_functions[0];

typedef BOOL (WINAPI *crypt_acquire_function)(DWORD *, LPCWSTR, LPCWSTR, DWORD, DWORD);
typedef BOOL (WINAPI *crypt_random_function)(DWORD, DWORD, BYTE *);

static void *find_in(const WCHAR *library, const WCHAR *name) {
    HMODULE module = GetModuleHandleW(library);
    if (!module) module = LoadLibraryW(library);
    return module ? (void *)GetProcAddressW(module, name) : NULL;
}

void *jolt_optional(struct optional_function *function) {
    if (function->looked) return function->address;
    if (function->library) function->address = find_in(function->library, function->name);
    if (!function->address) function->address = find_in(CORE_LIBRARY, function->name);
    function->looked = 1;
    return function->address;
}

int jolt_crypt_random(BYTE *buffer, DWORD length) {
    static DWORD provider;
    static int state;
    crypt_random_function generate = (crypt_random_function)jolt_optional(&jolt_crypt_generate);
    if (!state) {
        crypt_acquire_function acquire = (crypt_acquire_function)jolt_optional(&jolt_crypt_acquire);
        int ready = acquire && generate &&
            acquire(&provider, NULL, NULL, CRYPT_PROVIDER_RSA_FULL, CRYPT_VERIFY_CONTEXT);
        state = ready ? 1 : -1;
    }
    return state > 0 && generate(provider, length, buffer);
}
