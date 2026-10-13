#pragma once
#include <windows.h>

#define TRANSCRIPT_SIZE 65536
#define MESSAGE_CAPACITY 256
#define SESSION_COMMAND_CAPACITY 384

struct session {
    WCHAR folder[MAX_PATH];
    HWND window;
    int quiet;
    int finished;
    int refused;
    int hold;
    char command[SESSION_COMMAND_CAPACITY];
    DWORD start_time;
    DWORD first_output_time;
    DWORD last_output_time;
    DWORD finish_time;
    DWORD init_status;
    DWORD memory_at_start;
    char message[MESSAGE_CAPACITY];
    char *transcript;
    DWORD transcript_length;
};

extern struct session session;

void session_path(WCHAR *path, const WCHAR *name);
void session_write_file(const WCHAR *name, const char *first, DWORD first_length, const char *second, DWORD second_length);
void session_record_output(const char *bytes, DWORD count);
void session_finish(DWORD status, const char *message);
void session_save_result(void);
DWORD session_available_memory(void);
