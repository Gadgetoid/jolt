#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include "syscall.h"

int __clone(int (*func)(void *), void *stack, int flags, void *arg, ...) {
    (void)stack;
    if (!(flags & CLONE_VFORK)) return -ENOSYS;
    long pid = __syscall(SYS_fork);
    if (pid == 0) __syscall(SYS_exit, func(arg));
    return (int)pid;
}
