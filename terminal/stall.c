#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "stall.h"
#include "display.h"
#include "session.h"
#include "kernel.h"
#include "tty.h"
#include "script.h"

#define STALL_FILE L"stall.log"
#define STALL_MILLISECONDS 400
#define STALL_CAPACITY 16384
#define LINE_CAPACITY 160
#define TAIL_BYTES 48

static char report[STALL_CAPACITY];
static DWORD report_length;
static DWORD reported_key;

static const char *waiting_name(DWORD waiting) {
    switch (waiting) {
    case WAIT_NONE: return "running";
    case WAIT_TTY: return "tty";
    case WAIT_PIPE: return "pipe";
    case WAIT_CHILD: return "child";
    case WAIT_SIGNAL: return "signal";
    case WAIT_SLEEP: return "sleep";
    case WAIT_POLL: return "poll";
    case WAIT_FORK: return "fork";
    case WAIT_STOPPED: return "stopped";
    default: return "unknown";
    }
}

static void append(const char *format, ...) {
    char line[LINE_CAPACITY];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(line, sizeof line, format, arguments);
    va_end(arguments);
    if (length <= 0) return;
    if ((DWORD)length >= sizeof line) length = sizeof line - 1;
    if (report_length + (DWORD)length > STALL_CAPACITY) report_length = 0;
    memcpy(report + report_length, line, (size_t)length);
    report_length += (DWORD)length;
}

static void record(DWORD key, DWORD now) {
    const struct jolt_tty *console = jolt_tty(JOLT_CONSOLE);
    append("stall at %lu, %lu ms after key: input unread %ld, output undrained %ld, last output %lu ms ago, last paint %lu ms ago, "
        "foreground %ld\r\n", now, now - key, console->input_written - console->input_read,
        console->output_written - console->output_read, now - session.last_output_time, now - display_painted_tick(),
        console->foreground);
    if (session.transcript) {
        DWORD start = session.transcript_length > TAIL_BYTES ? session.transcript_length - TAIL_BYTES : 0;
        append("  output tail:");
        for (DWORD index = start; index < session.transcript_length; index++) {
            unsigned char value = (unsigned char)session.transcript[index];
            if (value >= ' ' && value < 0x7f) append("%c", value);
            else append("\\x%02x", value);
        }
        append("\r\n");
    }
    for (int slot = 0; slot < JOLT_PROCESSES; slot++) {
        const struct jolt_process *process = &jolt.kernel->processes[slot];
        if (process->state == PROCESS_FREE) continue;
        append("  pid %ld %.16s pgid %ld state %lu %s object %lx pending %lx\r\n", process->pid, process->name,
            process->pgid, process->state, waiting_name(process->waiting), (unsigned long)process->wait_object,
            (unsigned long)process->pending);
    }
    session_write_file(STALL_FILE, report, report_length, NULL, 0);
}

void stall_check(void) {
    if (!script_active()) return;
    DWORD key = display_key_tick(), now = GetTickCount();
    if (!key || key == reported_key || now - key < STALL_MILLISECONDS) return;
    reported_key = key;
    record(key, now);
}
