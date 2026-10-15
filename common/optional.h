#pragma once
#include <windows.h>

struct optional_function {
    const WCHAR *library;
    const WCHAR *name;
    int looked;
    void *address;
};

extern struct optional_function jolt_virtual_copy, jolt_all_keys, jolt_ws_control, jolt_crypt_acquire, jolt_crypt_generate,
    jolt_create_shortcut, jolt_load_bitmap, jolt_idle_reset, jolt_clipboard_read, jolt_store_information;
extern struct optional_function *const jolt_optional_functions[];
extern const int jolt_optional_count;

void *jolt_optional(struct optional_function *function);
int jolt_crypt_random(BYTE *buffer, DWORD length);
