#pragma once
#include "worker.h"

int guest_readable(DWORD pointer, DWORD length);
int guest_writable(DWORD pointer, DWORD length);
LONG guest_string(DWORD pointer, char *buffer, DWORD capacity);
int guest_copy_in(void *destination, DWORD source, DWORD length);
int guest_copy_out(DWORD destination, const void *source, DWORD length);
int add_range(DWORD start, DWORD length, DWORD flags);
int commit_pages(DWORD start, DWORD length);
int alias_pages(DWORD start, DWORD length, const void *source);
int protect_range(const struct guest_range *range);
void release_memory(void);
DWORD page_round_up(DWORD value);
