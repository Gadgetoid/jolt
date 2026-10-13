#ifndef JOLT_FILES_H
#define JOLT_FILES_H

#include <windows.h>
#include "kernel.h"
#include "linux.h"

enum {
    NODE_MISSING, NODE_FILE, NODE_DIRECTORY, NODE_APPLET, NODE_VIRTUAL_DIRECTORY,
    NODE_NULL, NODE_ZERO, NODE_TTY, NODE_PROC_FILE, NODE_RANDOM, NODE_CLIPBOARD, NODE_SERIAL
};

struct jolt_node {
    int kind;
    DWORD attributes;
    DWORD size;
    FILETIME modified;
};

int jolt_files_init(const WCHAR *folder);
void jolt_files_forget(void);
void jolt_files_drop(int description);
LONG jolt_lookup(const char *absolute, struct jolt_node *node);
LONG jolt_file_open(char *absolute, DWORD flags, DWORD *type);
LONG jolt_file_read(int description, void *buffer, DWORD length, LONG position);
LONG jolt_file_write(int description, const void *buffer, DWORD length, LONG position);
LONG jolt_file_seek(int description, LONG offset, DWORD origin);
LONG jolt_file_sync(int description);
LONG jolt_file_truncate(int description, const char *absolute, DWORD length);
LONG jolt_file_getdents(int description, void *buffer, DWORD capacity);
LONG jolt_file_statx(int description, const char *absolute, struct linux_statx *record);
LONG jolt_file_unlink(const char *absolute, int directory);
LONG jolt_file_mkdir(const char *absolute);
LONG jolt_file_rename(const char *source, const char *destination);
LONG jolt_file_access(const char *absolute, DWORD mode);
LONG jolt_file_chmod(const char *absolute, DWORD mode);
LONG jolt_file_touch(const char *absolute);
int jolt_is_applet(const char *name);
LONG jolt_file_statfs(struct linux_statfs64 *record);
LONG jolt_file_create(const char *absolute, const void *data, DWORD size);
LONG jolt_file_load(const char *absolute, BYTE **data, DWORD *size, DWORD limit);

#endif
