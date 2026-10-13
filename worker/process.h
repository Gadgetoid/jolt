#pragma once
#include "worker.h"
#include "image.h"

LONG worker_exec(const char *path, char **arguments, char **environment, struct guest_image *image);
LONG worker_fork(void);
int fork_child(void);
void close_local(int fd);
LONG resolve_at(DWORD directory, DWORD pointer, char *absolute, int *empty);
LONG tty_read(char *buffer, DWORD length, int nonblocking);
void update_signal_masks(void);
void suspend_signals(DWORD mask);
void resume_signals(void);
LONG deliver_signals(struct jolt_context *context, LONG result, DWORD original, DWORD number);
LONG wait_for_signal(void);
void unix_time(DWORD *seconds, DWORD *nanoseconds);
DWORD timespec_milliseconds(DWORD pointer, int wide, LONG *failure);
