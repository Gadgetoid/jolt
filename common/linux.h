#pragma once
#include <windows.h>
#include <stddef.h>

enum linux_syscall {
    SYS_exit = 1, SYS_fork = 2, SYS_read = 3, SYS_write = 4, SYS_open = 5, SYS_close = 6, SYS_creat = 8,
    SYS_link = 9, SYS_unlink = 10, SYS_execve = 11, SYS_chdir = 12, SYS_chmod = 15, SYS_lchown = 16,
    SYS_lseek = 19, SYS_getpid = 20, SYS_mount = 21, SYS_setuid = 23, SYS_getuid = 24, SYS_pause = 29, SYS_access = 33,
    SYS_sync = 36, SYS_kill = 37, SYS_rename = 38, SYS_mkdir = 39, SYS_rmdir = 40, SYS_dup = 41,
    SYS_pipe = 42, SYS_times = 43, SYS_brk = 45, SYS_setgid = 46, SYS_getgid = 47, SYS_geteuid = 49,
    SYS_getegid = 50, SYS_umount2 = 52, SYS_ioctl = 54, SYS_fcntl = 55, SYS_setpgid = 57, SYS_umask = 60, SYS_dup2 = 63,
    SYS_getppid = 64, SYS_getpgrp = 65, SYS_setsid = 66, SYS_setrlimit = 75, SYS_setitimer = 104, SYS_getitimer = 105, SYS_getpriority = 96, SYS_setpriority = 97, SYS_flock = 143, SYS_getrusage = 77,
    SYS_gettimeofday = 78, SYS_getgroups = 80, SYS_symlink = 83, SYS_readlink = 85, SYS_munmap = 91,
    SYS_truncate = 92, SYS_ftruncate = 93, SYS_fchmod = 94, SYS_fchown = 95, SYS_wait4 = 114,
    SYS_sysinfo = 116, SYS_fsync = 118, SYS_sigreturn = 119, SYS_clone = 120, SYS_uname = 122,
    SYS_mprotect = 125, SYS_getpgid = 132, SYS_fchdir = 133, SYS_llseek = 140, SYS_newselect = 142,
    SYS_readv = 145, SYS_writev = 146, SYS_getsid = 147, SYS_fdatasync = 148, SYS_sched_yield = 158,
    SYS_nanosleep = 162, SYS_mremap = 163, SYS_poll = 168, SYS_prctl = 172, SYS_rt_sigreturn = 173,
    SYS_rt_sigaction = 174, SYS_rt_sigprocmask = 175, SYS_rt_sigpending = 176, SYS_rt_sigsuspend = 179, SYS_statfs64 = 266, SYS_fstatfs64 = 267,
    SYS_pread64 = 180, SYS_pwrite64 = 181, SYS_chown = 182, SYS_getcwd = 183, SYS_sigaltstack = 186,
    SYS_vfork = 190, SYS_ugetrlimit = 191, SYS_mmap2 = 192, SYS_truncate64 = 193, SYS_ftruncate64 = 194,
    SYS_lchown32 = 198, SYS_getuid32 = 199, SYS_getgid32 = 200, SYS_geteuid32 = 201, SYS_getegid32 = 202,
    SYS_setreuid32 = 203, SYS_setregid32 = 204, SYS_getgroups32 = 205, SYS_fchown32 = 207,
    SYS_setresuid32 = 208, SYS_getresuid32 = 209, SYS_setresgid32 = 210, SYS_getresgid32 = 211,
    SYS_chown32 = 212, SYS_setuid32 = 213, SYS_setgid32 = 214, SYS_getdents64 = 217, SYS_madvise = 220,
    SYS_fcntl64 = 221, SYS_gettid = 224, SYS_tkill = 238, SYS_exit_group = 248, SYS_set_tid_address = 256,
    SYS_clock_gettime = 263, SYS_clock_getres = 264, SYS_clock_nanosleep = 265, SYS_tgkill = 268,
    SYS_openat = 322, SYS_mkdirat = 323, SYS_fchownat = 325, SYS_unlinkat = 328, SYS_renameat = 329,
    SYS_linkat = 330, SYS_symlinkat = 331, SYS_readlinkat = 332, SYS_fchmodat = 333, SYS_faccessat = 334,
    SYS_pselect6 = 335, SYS_ppoll = 336, SYS_set_robust_list = 338, SYS_utimensat = 348, SYS_dup3 = 358,
    SYS_pipe2 = 359, SYS_prlimit64 = 369, SYS_renameat2 = 382, SYS_getrandom = 384, SYS_execveat = 387,
    SYS_socket = 281, SYS_bind = 282, SYS_connect = 283, SYS_listen = 284, SYS_accept = 285,
    SYS_getsockname = 286, SYS_getpeername = 287, SYS_socketpair = 288, SYS_send = 289, SYS_sendto = 290,
    SYS_recv = 291, SYS_recvfrom = 292, SYS_shutdown = 293, SYS_setsockopt = 294, SYS_getsockopt = 295,
    SYS_sendmsg = 296, SYS_recvmsg = 297, SYS_accept4 = 366,
    SYS_statx = 397, SYS_clock_gettime64 = 403, SYS_clock_getres_time64 = 406,
    SYS_clock_nanosleep_time64 = 407, SYS_utimensat_time64 = 412, SYS_pselect6_time64 = 413,
    SYS_ppoll_time64 = 414, SYS_faccessat2 = 439,
    SYS_COUNT = 440
};

enum linux_errno {
    LINUX_EPERM = 1, LINUX_ENOENT = 2, LINUX_ESRCH = 3, LINUX_EINTR = 4, LINUX_EIO = 5, LINUX_E2BIG = 7,
    LINUX_ENOEXEC = 8, LINUX_EBADF = 9, LINUX_ECHILD = 10, LINUX_EAGAIN = 11, LINUX_ENOMEM = 12,
    LINUX_EACCES = 13, LINUX_EFAULT = 14, LINUX_EBUSY = 16, LINUX_EEXIST = 17, LINUX_EXDEV = 18, LINUX_ENODEV = 19, LINUX_ENOTDIR = 20,
    LINUX_EISDIR = 21, LINUX_EINVAL = 22, LINUX_ENFILE = 23, LINUX_EMFILE = 24, LINUX_ENOTTY = 25,
    LINUX_ETXTBSY = 26, LINUX_EFBIG = 27, LINUX_ENOSPC = 28, LINUX_ESPIPE = 29, LINUX_EPIPE = 32,
    LINUX_ERANGE = 34, LINUX_ENAMETOOLONG = 36, LINUX_ENOSYS = 38, LINUX_ENOTEMPTY = 39,
    LINUX_EOVERFLOW = 75, LINUX_EILSEQ = 84, LINUX_ENOTSOCK = 88, LINUX_EDESTADDRREQ = 89, LINUX_EMSGSIZE = 90,
    LINUX_EPROTOTYPE = 91, LINUX_ENOPROTOOPT = 92, LINUX_EPROTONOSUPPORT = 93, LINUX_EOPNOTSUPP = 95,
    LINUX_EAFNOSUPPORT = 97, LINUX_EADDRINUSE = 98, LINUX_EADDRNOTAVAIL = 99, LINUX_ENETDOWN = 100,
    LINUX_ENETUNREACH = 101, LINUX_ECONNABORTED = 103, LINUX_ECONNRESET = 104, LINUX_ENOBUFS = 105,
    LINUX_EISCONN = 106, LINUX_ENOTCONN = 107, LINUX_ETIMEDOUT = 110, LINUX_ECONNREFUSED = 111,
    LINUX_EHOSTUNREACH = 113, LINUX_EALREADY = 114, LINUX_EINPROGRESS = 115
};

#define LINUX_O_ACCMODE 3
#define LINUX_O_RDONLY 0
#define LINUX_O_WRONLY 1
#define LINUX_O_RDWR 2
#define LINUX_O_CREAT 0100
#define LINUX_O_EXCL 0200
#define LINUX_O_NOCTTY 0400
#define LINUX_O_TRUNC 01000
#define LINUX_O_APPEND 02000
#define LINUX_O_NONBLOCK 04000
#define LINUX_O_DIRECTORY 040000
#define LINUX_O_NOFOLLOW 0100000
#define LINUX_O_LARGEFILE 0400000
#define LINUX_O_CLOEXEC 02000000

#define LINUX_AT_FDCWD 0xffffff9cUL
#define LINUX_AT_REMOVEDIR 0x200
#define LINUX_AT_EMPTY_PATH 0x1000

#define LINUX_F_DUPFD 0
#define LINUX_F_GETFD 1
#define LINUX_F_SETFD 2
#define LINUX_F_GETFL 3
#define LINUX_F_SETFL 4
#define LINUX_F_GETLK 5
#define LINUX_F_SETLK 6
#define LINUX_F_SETLKW 7
#define LINUX_F_SETOWN 8
#define LINUX_F_GETOWN 9
#define LINUX_F_GETLK64 12
#define LINUX_F_SETLK64 13
#define LINUX_F_SETLKW64 14
#define LINUX_F_DUPFD_CLOEXEC 1030
#define LINUX_FD_CLOEXEC 1

#define LINUX_TCGETS 0x5401
#define LINUX_TCSETS 0x5402
#define LINUX_TCSETSW 0x5403
#define LINUX_TCSETSF 0x5404
#define LINUX_TCSBRK 0x5409
#define LINUX_TCXONC 0x540a
#define LINUX_TCFLSH 0x540b
#define LINUX_TIOCSCTTY 0x540e
#define LINUX_TIOCGPGRP 0x540f
#define LINUX_TIOCSPGRP 0x5410
#define LINUX_TIOCGWINSZ 0x5413
#define LINUX_TIOCSWINSZ 0x5414
#define LINUX_FIONREAD 0x541b
#define LINUX_FIONBIO 0x5421
#define LINUX_TIOCNOTTY 0x5422
#define LINUX_TIOCGSID 0x5429
#define LINUX_KIOCSOUND 0x4b2f
#define LINUX_KDMKTONE 0x4b30
#define LINUX_KDGKBTYPE 0x4b33
#define LINUX_KB_101 2
#define LINUX_TONE_CLOCK 1193180
#define LINUX_TCIFLUSH 0
#define LINUX_TCOFLUSH 1
#define LINUX_TCIOFLUSH 2
#define LINUX_TIOCEXCL 0x540c
#define LINUX_TIOCNXCL 0x540d
#define LINUX_TIOCOUTQ 0x5411
#define LINUX_TIOCMGET 0x5415
#define LINUX_TIOCMBIS 0x5416
#define LINUX_TIOCMBIC 0x5417
#define LINUX_TIOCMSET 0x5418
#define LINUX_TIOCSBRK 0x5427
#define LINUX_TIOCCBRK 0x5428
#define LINUX_TIOCM_DTR 0x002
#define LINUX_TIOCM_RTS 0x004
#define LINUX_TIOCM_CTS 0x020
#define LINUX_TIOCM_CAR 0x040
#define LINUX_TIOCM_RNG 0x080
#define LINUX_TIOCM_DSR 0x100

#define LINUX_ISTRIP 0x20
#define LINUX_INLCR 0x40
#define LINUX_IGNCR 0x80
#define LINUX_ICRNL 0x100
#define LINUX_IXON 0x400
#define LINUX_IXOFF 0x1000
#define LINUX_CBAUD 0x100f
#define LINUX_CBAUDEX 0x1000
#define LINUX_B9600 0xd
#define LINUX_B38400 0xf
#define LINUX_B115200 0x1002
#define LINUX_CSIZE 0x30
#define LINUX_CS5 0x0
#define LINUX_CS6 0x10
#define LINUX_CS7 0x20
#define LINUX_CS8 0x30
#define LINUX_CSTOPB 0x40
#define LINUX_CREAD 0x80
#define LINUX_PARENB 0x100
#define LINUX_PARODD 0x200
#define LINUX_HUPCL 0x400
#define LINUX_CLOCAL 0x800
#define LINUX_CRTSCTS 0x80000000UL
#define LINUX_OPOST 0x1
#define LINUX_ONLCR 0x4
#define LINUX_ISIG 0x1
#define LINUX_ICANON 0x2
#define LINUX_ECHO 0x8
#define LINUX_ECHOE 0x10
#define LINUX_ECHOK 0x20
#define LINUX_ECHOCTL 0x200
#define LINUX_ECHOKE 0x800
#define LINUX_IEXTEN 0x8000

enum linux_control_character {
    LINUX_VINTR = 0, LINUX_VQUIT = 1, LINUX_VERASE = 2, LINUX_VKILL = 3, LINUX_VEOF = 4, LINUX_VTIME = 5,
    LINUX_VMIN = 6, LINUX_VSWTC = 7, LINUX_VSTART = 8, LINUX_VSTOP = 9, LINUX_VSUSP = 10, LINUX_VEOL = 11,
    LINUX_VREPRINT = 12, LINUX_VDISCARD = 13, LINUX_VWERASE = 14, LINUX_VLNEXT = 15, LINUX_VEOL2 = 16,
    LINUX_NCCS = 19
};

#define LINUX_SIGHUP 1
#define LINUX_SIGINT 2
#define LINUX_SIGQUIT 3
#define LINUX_SIGKILL 9
#define LINUX_SIGSEGV 11
#define LINUX_SIGPIPE 13
#define LINUX_SIGALRM 14
#define LINUX_SIGTERM 15
#define LINUX_SIGCHLD 17
#define LINUX_SIGCONT 18
#define LINUX_SIGSTOP 19
#define LINUX_SIGTSTP 20
#define LINUX_SIGTTIN 21
#define LINUX_SIGTTOU 22
#define LINUX_SIGURG 23
#define LINUX_SIGWINCH 28
#define LINUX_SIGNAL_LIMIT 32
#define LINUX_SIGNAL_BIT(signal) (1UL << ((signal) - 1))
#define LINUX_DEFAULT_IGNORED (LINUX_SIGNAL_BIT(LINUX_SIGCHLD) | LINUX_SIGNAL_BIT(LINUX_SIGCONT) | \
    LINUX_SIGNAL_BIT(LINUX_SIGURG) | LINUX_SIGNAL_BIT(LINUX_SIGWINCH))
#define LINUX_DEFAULT_STOP (LINUX_SIGNAL_BIT(LINUX_SIGSTOP) | LINUX_SIGNAL_BIT(LINUX_SIGTSTP) | \
    LINUX_SIGNAL_BIT(LINUX_SIGTTIN) | LINUX_SIGNAL_BIT(LINUX_SIGTTOU))
#define LINUX_UNBLOCKABLE (LINUX_SIGNAL_BIT(LINUX_SIGKILL) | LINUX_SIGNAL_BIT(LINUX_SIGSTOP))
#define LINUX_SIG_DFL 0
#define LINUX_SIG_IGN 1
#define LINUX_SIG_BLOCK 0
#define LINUX_SIG_UNBLOCK 1
#define LINUX_SIG_SETMASK 2
#define LINUX_SA_RESTART 0x10000000UL
#define LINUX_SA_NODEFER 0x40000000UL
#define LINUX_SA_RESETHAND 0x80000000UL
#define LINUX_SIGSET_SIZE 8
#define LINUX_SIGINFO_SIZE 128
#define LINUX_CLONE_SIGNAL_MASK 0xff

#define LINUX_AF_UNIX 1
#define LINUX_AF_INET 2
#define LINUX_SOCK_STREAM 1
#define LINUX_SOCK_DGRAM 2
#define LINUX_SOCK_TYPE_MASK 0xf
#define LINUX_SHUT_RD 0
#define LINUX_SHUT_WR 1
#define LINUX_SHUT_RDWR 2
#define LINUX_SOCK_NONBLOCK LINUX_O_NONBLOCK
#define LINUX_SOCK_CLOEXEC LINUX_O_CLOEXEC
#define LINUX_SOL_SOCKET 1
#define LINUX_SO_REUSEADDR 2
#define LINUX_SO_TYPE 3
#define LINUX_SO_ERROR 4
#define LINUX_SO_BROADCAST 6
#define LINUX_SO_SNDBUF 7
#define LINUX_SO_RCVBUF 8
#define LINUX_SO_KEEPALIVE 9
#define LINUX_SO_LINGER 13
#define LINUX_SO_RCVTIMEO 20
#define LINUX_SO_SNDTIMEO 21
#define LINUX_IPPROTO_TCP 6
#define LINUX_TCP_NODELAY 1
#define LINUX_MSG_OOB 1
#define LINUX_MSG_PEEK 2
#define LINUX_MSG_DONTWAIT 0x40
#define LINUX_MSG_WAITALL 0x100
#define LINUX_MSG_NOSIGNAL 0x4000

#define LINUX_WNOHANG 1
#define LINUX_WUNTRACED 2
#define LINUX_WCONTINUED 8
#define LINUX_POLLIN 1
#define LINUX_POLLOUT 4
#define LINUX_POLLERR 8
#define LINUX_POLLHUP 16
#define LINUX_POLLNVAL 32

#define LINUX_PROT_READ 1
#define LINUX_PROT_WRITE 2
#define LINUX_MAP_FIXED 0x10
#define LINUX_MAP_ANONYMOUS 0x20

#define LINUX_CLOCK_REALTIME 0
#define LINUX_CLOCK_REALTIME_COARSE 5
#define LINUX_CLOCK_BOOTTIME 7
#define LINUX_CLOCK_REALTIME_ALARM 8
#define LINUX_TIMER_ABSTIME 1

#define LINUX_RLIMIT_STACK 3
#define LINUX_RLIMIT_NOFILE 7
#define LINUX_RLIM_INFINITY 0xffffffffUL

#define LINUX_S_IFIFO 0010000
#define LINUX_S_IFSOCK 0140000
#define LINUX_S_IFCHR 0020000
#define LINUX_S_IFDIR 0040000
#define LINUX_S_IFREG 0100000
#define LINUX_S_IFMT 0170000
#define LINUX_DT_CHR 2
#define LINUX_DT_DIR 4
#define LINUX_DT_REG 8
#define LINUX_STATX_BASIC_STATS 0x7ff

#define LINUX_WAIT_EXITED(code) (((code) & 255) << 8)
#define LINUX_WAIT_STOPPED(signal) (((signal) << 8) | 0x7f)
#define LINUX_WAIT_CONTINUED 0xffff

struct linux_termios {
    DWORD input_flags;
    DWORD output_flags;
    DWORD control_flags;
    DWORD local_flags;
    BYTE line_discipline;
    BYTE control[LINUX_NCCS];
};

struct linux_winsize {
    WORD rows, columns, x_pixels, y_pixels;
};

struct linux_timespec {
    LONG seconds;
    LONG nanoseconds;
};

struct linux_timespec64 {
    LONGLONG seconds;
    LONG nanoseconds;
    LONG padding;
};

struct linux_timeval {
    LONG seconds;
    LONG microseconds;
};

struct linux_pollfd {
    LONG fd;
    SHORT events;
    SHORT returned_events;
};

struct linux_iovec {
    DWORD base;
    DWORD length;
};

struct linux_sigaction {
    DWORD handler;
    DWORD flags;
    DWORD restorer;
    DWORD mask[2];
};

struct linux_rlimit {
    DWORD current, maximum;
};

struct linux_rlimit64 {
    ULONGLONG current, maximum;
};

struct linux_tms {
    LONG user, system, children_user, children_system;
};

struct linux_utsname {
    char system[65], node[65], release[65], version[65], machine[65], domain[65];
};

struct linux_sysinfo {
    LONG uptime;
    DWORD loads[3];
    DWORD total_memory, free_memory, shared_memory, buffer_memory, total_swap, free_swap;
    WORD processes, padding;
    DWORD total_high, free_high, memory_unit;
    char reserved[8];
};

struct linux_statx_timestamp {
    LONGLONG seconds;
    DWORD nanoseconds;
    LONG reserved;
};

struct linux_statx {
    DWORD mask, block_size;
    ULONGLONG attributes;
    DWORD links, user, group;
    WORD mode, spare;
    ULONGLONG inode, size, blocks, attributes_mask;
    struct linux_statx_timestamp accessed, created, changed, modified;
    DWORD device_major, device_minor, filesystem_major, filesystem_minor;
    ULONGLONG reserved[14];
};

struct linux_dirent64 {
    ULONGLONG inode;
    LONGLONG offset;
    WORD record_length;
    BYTE type;
    char name[];
};

#define LINUX_RUSAGE_SIZE 72
#define LINUX_ITIMER_REAL 0

struct linux_itimerval {
    struct linux_timeval interval;
    struct linux_timeval value;
};

struct linux_sockaddr_in {
    WORD family;
    WORD port;
    DWORD address;
    BYTE zero[8];
};

struct linux_msghdr {
    DWORD name;
    DWORD name_length;
    DWORD vectors;
    DWORD vector_count;
    DWORD control;
    DWORD control_length;
    DWORD flags;
};
#define LINUX_MSDOS_SUPER_MAGIC 0x4d44
#define LINUX_STATFS64_PACKED_SIZE 84

struct linux_statfs64 {
    DWORD type;
    DWORD block_size;
    ULONGLONG blocks;
    ULONGLONG free_blocks;
    ULONGLONG available_blocks;
    ULONGLONG files;
    ULONGLONG free_files;
    DWORD filesystem_id[2];
    DWORD name_length;
    DWORD fragment_size;
    DWORD flags;
    DWORD spare[4];
};

_Static_assert(sizeof(struct linux_termios) == 36, "termios size");
_Static_assert(sizeof(struct linux_timespec64) == 16, "timespec64 size");
_Static_assert(sizeof(struct linux_pollfd) == 8, "pollfd size");
_Static_assert(sizeof(struct linux_sigaction) == 20, "sigaction size");
_Static_assert(sizeof(struct linux_utsname) == 390, "utsname size");
_Static_assert(sizeof(struct linux_sysinfo) == 64, "sysinfo size");
_Static_assert(sizeof(struct linux_statx) == 256, "statx size");
_Static_assert(offsetof(struct linux_statx, modified) == 112, "statx layout");
_Static_assert(offsetof(struct linux_dirent64, name) == 19, "dirent64 layout");
_Static_assert(sizeof(struct linux_statfs64) == 88, "statfs64 size");
