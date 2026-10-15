#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "text.h"
#include "diagnostic.h"
#include "session.h"
#include "optional.h"

#define DIAGNOSTIC_FILE L"diagnostic.txt"
#define DIAGNOSTIC_CAPACITY 1024
#define DETAIL_CAPACITY 64
#define MISSING_CAPACITY 192
#define ISSUES_URL "https://github.com/Gadgetoid/jolt/issues"

static void system_text(UINT action, char *text) {
    WCHAR wide[DETAIL_CAPACITY];
    wide[0] = 0;
    if (!SystemParametersInfoW(action, sizeof wide, wide, 0)) wide[0] = 0;
    wide[DETAIL_CAPACITY - 1] = 0;
    jolt_ascii_from_wide(wide, text, DETAIL_CAPACITY);
}

static void missing_functions(char *text, size_t capacity) {
    size_t used = 0;
    text[0] = 0;
    for (int index = 0; index < jolt_optional_count; index++) {
        struct optional_function *function = jolt_optional_functions[index];
        if (jolt_optional(function)) continue;
        char name[DETAIL_CAPACITY];
        jolt_ascii_from_wide(function->name, name, sizeof name);
        int written = snprintf(text + used, capacity - used, "%s%s", used ? ", " : "", name);
        if (written < 0 || used + (size_t)written >= capacity) break;
        used += (size_t)written;
    }
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
        "Problem: %s\r\n"
        "JoLT %s on Windows CE %lu.%02lu build %lu\r\n"
        "Device: %s (%s)\r\n"
        "CPU: type %lu, architecture %u, level %u, revision %04x\r\n"
        "Memory: %lu KB free of %lu KB, screen %dx%d %d-bit\r\n"
        "Missing functions: %s\r\n"
        "Please report at " ISSUES_URL "\r\n"
        "Saved as JoLT\\diagnostic.txt\r\n",
        problem, JOLT_VERSION, version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber, oem, platform,
        system.dwProcessorType, system.wProcessorArchitecture, system.wProcessorLevel, system.wProcessorRevision,
        memory.dwAvailPhys / 1024, memory.dwTotalPhys / 1024, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        depth, missing);
}

static void report(const char *problem, int show) {
    char text[DIAGNOSTIC_CAPACITY];
    int length = describe(problem, text, sizeof text);
    if (length <= 0) return;
    if (length >= (int)sizeof text) length = sizeof text - 1;
    session_write_file(DIAGNOSTIC_FILE, text, (DWORD)length, NULL, 0);
    if (!show) return;
    WCHAR wide[DIAGNOSTIC_CAPACITY];
    jolt_wide_from_ascii(text, wide, DIAGNOSTIC_CAPACITY);
    MessageBoxW(session.window, wide, L"JoLT", MB_OK | MB_ICONERROR);
}

void diagnostic_report(const char *problem) {
    report(problem, !session.quiet);
}

void diagnostic_show(const char *problem) {
    report(problem, 1);
}
