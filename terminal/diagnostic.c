#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "diagnostic.h"
#include "session.h"
#include "optional.h"

#define DIAGNOSTIC_FILE L"diagnostic.txt"
#define DIAGNOSTIC_CAPACITY 1024
#define DETAIL_CAPACITY 64
#define MISSING_CAPACITY 192
#define ISSUES_URL "https://github.com/Gadgetoid/jolt/issues"

static void narrow(const WCHAR *wide, char *text, size_t capacity) {
    size_t length = 0;
    for (; wide[length] && length + 1 < capacity; length++)
        text[length] = wide[length] < 0x80 ? (char)wide[length] : '?';
    text[length] = 0;
}

static void system_text(UINT action, char *text) {
    WCHAR wide[DETAIL_CAPACITY];
    wide[0] = 0;
    if (!SystemParametersInfoW(action, sizeof wide, wide, 0)) wide[0] = 0;
    wide[DETAIL_CAPACITY - 1] = 0;
    narrow(wide, text, DETAIL_CAPACITY);
}

static const WCHAR *const optional_functions[] = {
    L"AllKeys", L"VirtualCopy", L"CryptAcquireContextW", L"CryptGenRandom", L"SHCreateShortcut", L"SHLoadDIBitmap",
    L"SystemIdleTimerReset", L"GetClipboardDataAlloc", L"GetStoreInformation",
};

static void missing_functions(char *text, size_t capacity) {
    size_t used = 0;
    text[0] = 0;
    for (size_t index = 0; index < sizeof optional_functions / sizeof optional_functions[0]; index++) {
        if (jolt_optional(optional_functions[index])) continue;
        char name[DETAIL_CAPACITY];
        narrow(optional_functions[index], name, sizeof name);
        int written = snprintf(text + used, capacity - used, "%s%s", used ? ", " : "", name);
        if (written < 0 || used + (size_t)written >= capacity) break;
        used += (size_t)written;
    }
    if (!jolt_optional_in(L"winsock.dll", L"WsControl") && !jolt_optional(L"WsControl"))
        snprintf(text + used, capacity - used, "%sWsControl", used ? ", " : "");
    if (!text[0]) snprintf(text, capacity, "none");
}

static int describe(const char *problem, char *text, size_t capacity) {
    OSVERSIONINFOW version;
    memset(&version, 0, sizeof version);
    version.dwOSVersionInfoSize = sizeof version;
    GetVersionExW(&version);
    SYSTEM_INFO system;
    memset(&system, 0, sizeof system);
    GetSystemInfo(&system);
    MEMORYSTATUS memory;
    memset(&memory, 0, sizeof memory);
    memory.dwLength = sizeof memory;
    GlobalMemoryStatus(&memory);
    HDC screen = GetDC(NULL);
    int depth = screen ? GetDeviceCaps(screen, BITSPIXEL) : 0;
    if (screen) ReleaseDC(NULL, screen);
    char oem[DETAIL_CAPACITY], platform[DETAIL_CAPACITY], missing[MISSING_CAPACITY];
    missing_functions(missing, sizeof missing);
    system_text(SPI_GETOEMINFO, oem);
    system_text(SPI_GETPLATFORMTYPE, platform);
    return snprintf(text, capacity,
        "JoLT diagnostic report\r\n\r\n"
        "Problem: %s\r\n\r\n"
        "JoLT %s\r\n"
        "Windows CE %lu.%02lu build %lu\r\n"
        "Device: %s\r\n"
        "Platform: %s\r\n"
        "CPU: type %lu, architecture %u, level %u, revision %04x\r\n"
        "Screen: %dx%d, %d-bit\r\n"
        "Memory: %lu KB free of %lu KB\r\n"
        "Missing functions: %s\r\n\r\n"
        "Please report at " ISSUES_URL " (saved as JoLT\\diagnostic.txt)\r\n",
        problem, JOLT_VERSION, version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber, oem, platform,
        system.dwProcessorType, system.wProcessorArchitecture, system.wProcessorLevel, system.wProcessorRevision,
        GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), depth, memory.dwAvailPhys / 1024,
        memory.dwTotalPhys / 1024, missing);
}

static void report(const char *problem, int show) {
    char text[DIAGNOSTIC_CAPACITY];
    int length = describe(problem, text, sizeof text);
    if (length <= 0) return;
    if (length >= (int)sizeof text) length = sizeof text - 1;
    session_write_file(DIAGNOSTIC_FILE, text, (DWORD)length, NULL, 0);
    if (!show) return;
    WCHAR wide[DIAGNOSTIC_CAPACITY];
    for (int index = 0; index <= length; index++) wide[index] = (WCHAR)(unsigned char)text[index];
    MessageBoxW(session.window, wide, L"JoLT", MB_OK | MB_ICONERROR);
}

void diagnostic_report(const char *problem) {
    report(problem, !session.quiet);
}

void diagnostic_show(const char *problem) {
    report(problem, 1);
}
