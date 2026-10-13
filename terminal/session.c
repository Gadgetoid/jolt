#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "session.h"
#include "display.h"
#include "linux.h"

#define SCREEN_TEXT_CAPACITY 8192
#define WAIT_SIGNAL_MASK 0x7f
#define WAIT_CODE_SHIFT 8

struct session session;

void session_path(WCHAR *path, const WCHAR *name) {
    wcscpy(path, session.folder);
    wcscat(path, name);
}

void session_write_file(const WCHAR *name, const char *first, DWORD first_length, const char *second, DWORD second_length) {
    WCHAR path[MAX_PATH];
    session_path(path, name);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    if (first_length) WriteFile(file, first, first_length, &written, NULL);
    if (second_length) WriteFile(file, second, second_length, &written, NULL);
    CloseHandle(file);
}

void session_record_output(const char *bytes, DWORD count) {
    DWORD now = GetTickCount();
    if (!session.first_output_time) session.first_output_time = now;
    session.last_output_time = now;
    if (!session.transcript || session.transcript_length >= TRANSCRIPT_SIZE) return;
    DWORD room = TRANSCRIPT_SIZE - session.transcript_length;
    DWORD copied = count < room ? count : room;
    memcpy(session.transcript + session.transcript_length, bytes, copied);
    session.transcript_length += copied;
}

void session_finish(DWORD status, const char *message) {
    if (session.finished) return;
    session.finished = 1;
    session.finish_time = GetTickCount();
    session.init_status = status;
    if (message) {
        strncpy(session.message, message, MESSAGE_CAPACITY - 1);
        session.message[MESSAGE_CAPACITY - 1] = 0;
    }
}

DWORD session_available_memory(void) {
    MEMORYSTATUS status;
    memset(&status, 0, sizeof status);
    status.dwLength = sizeof status;
    GlobalMemoryStatus(&status);
    return status.dwAvailPhys;
}

void session_save_result(void) {
    DWORD code = (session.init_status >> WAIT_CODE_SHIFT) & 255, signal = session.init_status & WAIT_SIGNAL_MASK;
    int passed = !session.refused && session.finished && !signal;
    DWORD first_output = session.first_output_time ? session.first_output_time - session.start_time : 0;
    DWORD finish = session.finish_time ? session.finish_time : GetTickCount();
    char header[MESSAGE_CAPACITY * 2];
    int length = snprintf(header, sizeof header,
        "JOLT_SHELL %s exit=%lu signal=%lu first_output_ms=%lu total_ms=%lu memory_kb=%lu/%lu splash=%d%s%s\r\n",
        passed ? "PASS" : "FAIL", code, signal, first_output, finish - session.start_time,
        session.memory_at_start / 1024, session_available_memory() / 1024, display_splash_loaded(),
        session.message[0] ? " " : "", session.message);
    char *text = LocalAlloc(LMEM_FIXED, SCREEN_TEXT_CAPACITY);
    DWORD text_length = text ? display_screen_text(text, SCREEN_TEXT_CAPACITY) : 0;
    session_write_file(L"Jolt.log", header, (DWORD)length, text, text_length);
    if (text) LocalFree(text);
    if (session.transcript) session_write_file(L"JoltTty.log", session.transcript, session.transcript_length, NULL, 0);
}
