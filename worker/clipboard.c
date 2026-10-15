#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <jornada/extras.h>
#include "optional.h"
#include "clipboard.h"
#include "worker.h"
#include "files.h"
#include "text.h"

#define UTF8_BYTES_PER_UNIT 3

static DWORD sequence;

typedef HANDLE (WINAPI *clipboard_function)(UINT);

static char *snapshot(DWORD *size) {
    *size = 0;
    if (!OpenClipboard(NULL)) return NULL;
    clipboard_function read = (clipboard_function)jolt_optional(L"GetClipboardDataAlloc");
    WCHAR *wide = read ? read(CF_UNICODETEXT) : NULL;
    CloseClipboard();
    if (!wide) return NULL;
    DWORD capacity = (DWORD)wcslen(wide) * UTF8_BYTES_PER_UNIT + 1;
    char *text = LocalAlloc(LMEM_FIXED, capacity);
    if (text) *size = jolt_utf8_from_wide(wide, text, capacity);
    LocalFree(wide);
    return text;
}

LONG clipboard_open(char *absolute, DWORD flags) {
    snprintf(absolute, JOLT_GUEST_PATH, JOLT_CLIPBOARD_PREFIX "%ld-%lu", (long)self->pid,
        (unsigned long)(GetTickCount() + sequence++));
    int keep = (flags & LINUX_O_ACCMODE) != LINUX_O_WRONLY || (flags & LINUX_O_APPEND);
    DWORD size = 0;
    char *text = keep ? snapshot(&size) : NULL;
    LONG result = jolt_file_create(absolute, text, size);
    if (text) LocalFree(text);
    return result;
}
