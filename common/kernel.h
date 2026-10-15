#pragma once
#include <windows.h>
#include "linux.h"

#define JOLT_KERNEL_MAGIC 0x4a4f4c4bUL
#define JOLT_KERNEL_VERSION 16
#define JOLT_PROCESSES 16
#define JOLT_DESCRIPTIONS 48
#define JOLT_PIPES 12
#define JOLT_PIPE_SIZE 4096
#define JOLT_SHUTDOWN_READ 1
#define JOLT_SHUTDOWN_WRITE 2
#define JOLT_FDS 64
#define JOLT_TTY_SIZE 8192
#define JOLT_INPUT_SIZE 1024
#define JOLT_TTYS 5
#define JOLT_CONSOLE 0
#define JOLT_NO_TTY (-1)
#define JOLT_TTY_LINE 256
#define JOLT_GUEST_PATH 256
#define JOLT_ARGUMENTS 1024
#define JOLT_COMMAND_LINE 128
#define JOLT_DIGEST_SIZE 32
#define JOLT_ENTROPY_SIZE 32
#define JOLT_TTY_MESSAGE (WM_USER + 1)
#define JOLT_SOUND_MESSAGE (WM_USER + 2)
#define JOLT_CLIPBOARD_MESSAGE (WM_USER + 3)
#define JOLT_WARM_MESSAGE (WM_USER + 4)
#define JOLT_WARM_IMAGES 8
#define JOLT_DIAGNOSTIC 192
#define JOLT_MOUNTS 8
#define JOLT_SERIAL_PORTS 4
#define JOLT_SERIAL_BUFFER 4096
#define JOLT_MOUNT_SOURCE 160
#define JOLT_CLIPBOARD_PREFIX "/tmp/.clipboard-"

enum { PROCESS_FREE, PROCESS_STARTING, PROCESS_RUNNING, PROCESS_ZOMBIE };
enum { START_EXEC = 1, START_FORK };
enum {
    DESCRIPTION_FREE, DESCRIPTION_FILE, DESCRIPTION_DIRECTORY, DESCRIPTION_PIPE_READ,
    DESCRIPTION_PIPE_WRITE, DESCRIPTION_TTY, DESCRIPTION_NULL, DESCRIPTION_ZERO, DESCRIPTION_PROC, DESCRIPTION_SOCKET, DESCRIPTION_RANDOM,
    DESCRIPTION_STREAM, DESCRIPTION_SERIAL, DESCRIPTION_EVENT, DESCRIPTION_PTY_MASTER
};
enum { TTY_FREE, TTY_CONSOLE, TTY_PTY };
enum { SERIAL_CLOSED, SERIAL_OPENING, SERIAL_OPEN, SERIAL_FAILED };
#define JOLT_NETWORK_BUFFER 4096

enum { NETWORK_IDLE, NETWORK_PENDING, NETWORK_DONE };
enum {
    NETWORK_SOCKET = 1, NETWORK_CLOSE, NETWORK_BIND, NETWORK_CONNECT, NETWORK_LISTEN, NETWORK_ACCEPT,
    NETWORK_SEND, NETWORK_RECEIVE, NETWORK_SHUTDOWN, NETWORK_SET_OPTION, NETWORK_GET_OPTION,
    NETWORK_LOCAL_NAME, NETWORK_PEER_NAME, NETWORK_PENDING_BYTES, NETWORK_READINESS, NETWORK_WAIT
};
enum { NETWORK_READABLE = 1, NETWORK_WRITABLE = 2, NETWORK_FAILED = 4 };

struct jolt_network_request {
    volatile LONG state;
    DWORD operation;
    DWORD handle;
    DWORD arguments[3];
    LONG result;
    LONG error;
    DWORD length;
    DWORD has_address;
    BYTE address[16];
    BYTE data[JOLT_NETWORK_BUFFER];
};

enum { SPARE_NONE, SPARE_STARTING, SPARE_READY, SPARE_ASSIGNED };
enum { WAIT_NONE, WAIT_TTY, WAIT_PIPE, WAIT_CHILD, WAIT_SIGNAL, WAIT_SLEEP, WAIT_POLL, WAIT_FORK, WAIT_STOPPED };

struct jolt_description {
    LONG references;
    DWORD type;
    DWORD flags;
    DWORD offset;
    DWORD generation;
    DWORD pipe;
    DWORD output;
    ULONGLONG counter;
    DWORD shutdown;
    DWORD socket;
    DWORD socket_type;
    LONG tty;
    char path[JOLT_GUEST_PATH];
};

struct jolt_process {
    DWORD state;
    DWORD start;
    LONG pid, ppid, pgid, sid;
    LONG ctty;
    DWORD host;
    LONG source;
    DWORD status;
    DWORD pending;
    DWORD fatal;
    DWORD ignored;
    DWORD waiting;
    DWORD signal_time;
    DWORD stopped;
    DWORD stop_signal;
    DWORD stop_reported;
    DWORD continued;
    DWORD pages;
    DWORD start_tick;
    DWORD command_length;
    char command[JOLT_COMMAND_LINE];
    DWORD cloexec[2];
    BYTE fds[JOLT_FDS];
    char name[16];
};

struct jolt_pipe {
    LONG readers, writers;
    DWORD read, written;
    char data[JOLT_PIPE_SIZE];
};

struct jolt_mount {
    char target[JOLT_GUEST_PATH];
    char source[JOLT_MOUNT_SOURCE];
};

struct jolt_serial {
    LONG users;
    LONG state;
    DWORD error;
    LONG settings_changed;
    LONG purge;
    LONG break_state;
    DWORD modem_lines;
    LONG modem_changed;
    DWORD modem_status;
    struct linux_termios termios;
    volatile LONG received_written, received_read, sent_written, sent_read;
    char received[JOLT_SERIAL_BUFFER];
    char sent[JOLT_SERIAL_BUFFER];
};

struct jolt_tty {
    LONG state;
    LONG masters, slaves;
    LONG locked;
    LONG hung_up;
    LONG opened;
    LONG session, foreground;
    WORD rows, columns;
    struct linux_termios termios;
    volatile LONG output_written, output_read, output_notified;
    volatile LONG input_written, input_read;
    LONG input_eof;
    DWORD line_length;
    char line[JOLT_TTY_LINE];
    char output[JOLT_TTY_SIZE];
    char input[JOLT_INPUT_SIZE];
};

struct jolt_warm {
    DWORD key, size;
    LONG used;
};

struct jolt_kernel {
    DWORD magic, version, size;
    DWORD window, launcher, trace;
    LONG next_pid;
    DWORD image_size;
    LONG spare_state, spare_slot;
    DWORD spare_host;
    struct jolt_tty ttys[JOLT_TTYS];
    LONG active_tty;
    char arguments[JOLT_ARGUMENTS];
    struct jolt_process processes[JOLT_PROCESSES];
    struct jolt_description descriptions[JOLT_DESCRIPTIONS];
    struct jolt_pipe pipes[JOLT_PIPES];
    struct jolt_network_request network[JOLT_PROCESSES];
    struct jolt_mount mounts[JOLT_MOUNTS];
    char clipboard_commit[JOLT_GUEST_PATH];
    BYTE entropy[JOLT_ENTROPY_SIZE];
    struct jolt_serial serial[JOLT_SERIAL_PORTS];
    struct jolt_warm warm[JOLT_WARM_IMAGES];
    LONG warm_clock;
    char diagnostic[JOLT_DIAGNOSTIC];
};

struct jolt_runtime {
    struct jolt_kernel *kernel;
    HANDLE mapping, lock, image_mapping, spare, refill, network, serial;
    const BYTE *image;
    HANDLE wake[JOLT_PROCESSES];
    DWORD identifier;
};

extern struct jolt_runtime jolt;

void jolt_object_name(WCHAR *name, const WCHAR *prefix, DWORD identifier, int index);
int jolt_kernel_open(DWORD identifier, int create, DWORD image_size);
void jolt_kernel_close(void);
void jolt_lock(void);
void jolt_unlock(void);
void jolt_wake(int slot);
void jolt_wake_all(void);
int jolt_find_pid(LONG pid);
void jolt_signal_process(int slot, int signal);
void jolt_signal_group(LONG group, int signal);
void jolt_notify_parent(int slot);
int jolt_description_alloc(DWORD type, DWORD flags, const char *path);
void jolt_description_release(int index);
int jolt_fd_install(struct jolt_process *process, int minimum, int description, int cloexec);
void jolt_fd_close(struct jolt_process *process, int fd);
void jolt_process_exit(int slot, DWORD status);
void jolt_image_name(WCHAR *name, DWORD key);
void jolt_image_touch(DWORD key, DWORD size);
LONG jolt_image_used(DWORD key);
void jolt_tty_default_settings(struct linux_termios *settings);
int jolt_tty_open(void);
DWORD jolt_tty_write(int tty, const char *bytes, DWORD length, int block);
DWORD jolt_tty_master_write(int tty, const char *bytes, DWORD length);
void jolt_tty_input(int tty, char character);
void jolt_tty_discard_input(int tty);
void jolt_tty_hang_up(int tty);
void jolt_tty_resize(int tty, WORD rows, WORD columns);
void jolt_tty_release_end(int tty, int master);
