#include <windows.h>
#include "priority.h"
#include "worker.h"

static int current = THREAD_PRIORITY_NORMAL;

static int in_foreground_of(LONG tty) {
    return tty >= JOLT_CONSOLE && tty < JOLT_TTYS && self->ctty == tty && self->pgid == kernel()->ttys[tty].foreground;
}

void priority_update(void) {
    int foreground = in_foreground_of(JOLT_CONSOLE) || in_foreground_of(kernel()->active_tty);
    int wanted = foreground ? THREAD_PRIORITY_NORMAL : THREAD_PRIORITY_BELOW_NORMAL;
    if (wanted == current) return;
    if (SetThreadPriority(GetCurrentThread(), wanted)) current = wanted;
}
