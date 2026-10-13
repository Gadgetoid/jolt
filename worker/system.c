#include <windows.h>
#include <string.h>
#include "worker.h"
#include "memory.h"
#include "process.h"
#include "syscalls.h"
#include "random.h"

#define UNIX_EPOCH_FILETIME 116444736000000000ULL
#define FILETIME_TICKS_PER_SECOND 10000000ULL
#define NANOSECONDS_PER_SECOND 1000000000UL
#define NANOSECONDS_PER_MILLISECOND 1000000UL
#define MILLISECONDS_PER_SECOND 1000UL
#define CLOCK_TICKS_PER_SECOND 100
#define LONGEST_SLEEP_SECONDS 2000000
#define CLOCK_RESOLUTION_NANOSECONDS 1000000
#define CLOCK_DRIFT_MILLISECONDS 1000

static ULONGLONG system_milliseconds(void) {
    SYSTEMTIME now;
    FILETIME time;
    GetSystemTime(&now);
    SystemTimeToFileTime(&now, &time);
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return (value.QuadPart - UNIX_EPOCH_FILETIME) / (FILETIME_TICKS_PER_SECOND / MILLISECONDS_PER_SECOND);
}

void unix_time(DWORD *seconds, DWORD *nanoseconds) {
    static ULONGLONG base_milliseconds;
    static DWORD base_tick;
    static int anchored;
    ULONGLONG system = system_milliseconds();
    DWORD tick = GetTickCount();
    ULONGLONG estimate = base_milliseconds + (DWORD)(tick - base_tick);
    LONGLONG drift = (LONGLONG)(estimate - system);
    if (!anchored || drift < 0 || drift >= CLOCK_DRIFT_MILLISECONDS) {
        base_milliseconds = estimate = system;
        base_tick = tick;
        anchored = 1;
    }
    *seconds = (DWORD)(estimate / MILLISECONDS_PER_SECOND);
    *nanoseconds = (DWORD)(estimate % MILLISECONDS_PER_SECOND) * NANOSECONDS_PER_MILLISECOND;
}

static void monotonic_time(DWORD *seconds, DWORD *nanoseconds) {
    DWORD ticks = GetTickCount();
    *seconds = ticks / MILLISECONDS_PER_SECOND;
    *nanoseconds = (ticks % MILLISECONDS_PER_SECOND) * NANOSECONDS_PER_MILLISECOND;
}

static int is_realtime(DWORD clock) {
    return clock == LINUX_CLOCK_REALTIME || clock == LINUX_CLOCK_REALTIME_COARSE || clock == LINUX_CLOCK_REALTIME_ALARM;
}

DWORD timespec_milliseconds(DWORD pointer, int wide, LONG *failure) {
    *failure = 0;
    if (!pointer) return INFINITE;
    LONGLONG seconds;
    DWORD nanoseconds;
    if (wide) {
        struct linux_timespec64 value;
        if (!guest_copy_in(&value, pointer, sizeof value)) *failure = error(LINUX_EFAULT);
        seconds = value.seconds;
        nanoseconds = (DWORD)value.nanoseconds;
    } else {
        struct linux_timespec value;
        if (!guest_copy_in(&value, pointer, sizeof value)) *failure = error(LINUX_EFAULT);
        seconds = (DWORD)value.seconds;
        nanoseconds = (DWORD)value.nanoseconds;
    }
    if (*failure) return 0;
    if (seconds < 0 || seconds > LONGEST_SLEEP_SECONDS) return INFINITE - 1;
    if (nanoseconds >= NANOSECONDS_PER_SECOND) {
        *failure = error(LINUX_EINVAL);
        return 0;
    }
    return (DWORD)seconds * MILLISECONDS_PER_SECOND + (nanoseconds + NANOSECONDS_PER_MILLISECOND - 1) / NANOSECONDS_PER_MILLISECOND;
}

static LONG put_time(DWORD pointer, DWORD seconds, DWORD nanoseconds, int wide) {
    if (wide) {
        struct linux_timespec64 value = { seconds, (LONG)nanoseconds, 0 };
        return guest_copy_out(pointer, &value, sizeof value) ? 0 : error(LINUX_EFAULT);
    }
    struct linux_timespec value = { (LONG)seconds, (LONG)nanoseconds };
    return guest_copy_out(pointer, &value, sizeof value) ? 0 : error(LINUX_EFAULT);
}

static LONG clock_get(DWORD clock, DWORD pointer, int wide) {
    DWORD seconds, nanoseconds;
    if (is_realtime(clock)) unix_time(&seconds, &nanoseconds);
    else monotonic_time(&seconds, &nanoseconds);
    return put_time(pointer, seconds, nanoseconds, wide);
}

LONG sys_clock_gettime(const DWORD *arguments) {
    return clock_get(arguments[0], arguments[1], 0);
}

LONG sys_clock_gettime64(const DWORD *arguments) {
    return clock_get(arguments[0], arguments[1], 1);
}

static LONG clock_resolution(DWORD pointer, int wide) {
    return pointer ? put_time(pointer, 0, CLOCK_RESOLUTION_NANOSECONDS, wide) : 0;
}

LONG sys_clock_getres(const DWORD *arguments) {
    return clock_resolution(arguments[1], 0);
}

LONG sys_clock_getres_time64(const DWORD *arguments) {
    return clock_resolution(arguments[1], 1);
}

static LONG sleep_for(DWORD milliseconds) {
    DWORD began = GetTickCount();
    for (;;) {
        DWORD left = remaining(began, milliseconds);
        if (!left) return 0;
        jolt_lock();
        int interrupted = signal_ready();
        self->waiting = interrupted ? WAIT_NONE : WAIT_SLEEP;
        jolt_unlock();
        if (interrupted) return error(LINUX_EINTR);
        block_until(left);
    }
}

static LONG sleep_request(DWORD request, DWORD remaining_pointer, int wide) {
    LONG failure;
    DWORD milliseconds = timespec_milliseconds(request, wide, &failure);
    if (failure) return failure;
    LONG result = sleep_for(milliseconds);
    if (remaining_pointer) put_time(remaining_pointer, 0, 0, wide);
    return result;
}

LONG sys_nanosleep(const DWORD *arguments) {
    return sleep_request(arguments[0], arguments[1], 0);
}

LONG sys_clock_nanosleep(const DWORD *arguments) {
    if (arguments[1] & LINUX_TIMER_ABSTIME) return error(LINUX_EINVAL);
    return sleep_request(arguments[2], arguments[3], 0);
}

LONG sys_clock_nanosleep_time64(const DWORD *arguments) {
    if (arguments[1] & LINUX_TIMER_ABSTIME) return error(LINUX_EINVAL);
    return sleep_request(arguments[2], arguments[3], 1);
}

LONG sys_gettimeofday(const DWORD *arguments) {
    if (!arguments[0]) return 0;
    DWORD seconds, nanoseconds;
    unix_time(&seconds, &nanoseconds);
    struct linux_timeval value = { (LONG)seconds, (LONG)(nanoseconds / 1000) };
    return guest_copy_out(arguments[0], &value, sizeof value) ? 0 : error(LINUX_EFAULT);
}

LONG sys_times(const DWORD *arguments) {
    LONG ticks = (LONG)(GetTickCount() / (MILLISECONDS_PER_SECOND / CLOCK_TICKS_PER_SECOND));
    if (arguments[0]) {
        struct linux_tms usage = { ticks, 0, 0, 0 };
        if (!guest_copy_out(arguments[0], &usage, sizeof usage)) return error(LINUX_EFAULT);
    }
    return ticks;
}

LONG sys_getrusage(const DWORD *arguments) {
    if (!guest_writable(arguments[1], LINUX_RUSAGE_SIZE)) return error(LINUX_EFAULT);
    memset((void *)arguments[1], 0, LINUX_RUSAGE_SIZE);
    return 0;
}

LONG sys_uname(const DWORD *arguments) {
    struct linux_utsname name;
    memset(&name, 0, sizeof name);
    strcpy(name.system, "Linux");
    strcpy(name.node, "jornada");
    strncpy(name.release, JOLT_VERSION "-jolt", sizeof name.release - 1);
    strcpy(name.version, "JoLT native compatibility layer on Windows CE 3.0");
    strcpy(name.machine, "armv4l");
    strcpy(name.domain, "(none)");
    return guest_copy_out(arguments[0], &name, sizeof name) ? 0 : error(LINUX_EFAULT);
}

static WORD process_count(void) {
    WORD count = 0;
    for (int index = 0; index < JOLT_PROCESSES; index++)
        if (kernel()->processes[index].state != PROCESS_FREE) count++;
    return count;
}

LONG sys_sysinfo(const DWORD *arguments) {
    MEMORYSTATUS memory;
    memset(&memory, 0, sizeof memory);
    memory.dwLength = sizeof memory;
    GlobalMemoryStatus(&memory);
    struct linux_sysinfo information;
    memset(&information, 0, sizeof information);
    information.uptime = (LONG)(GetTickCount() / MILLISECONDS_PER_SECOND);
    information.total_memory = memory.dwTotalPhys;
    information.free_memory = memory.dwAvailPhys;
    information.processes = process_count();
    information.memory_unit = 1;
    return guest_copy_out(arguments[0], &information, sizeof information) ? 0 : error(LINUX_EFAULT);
}

static DWORD resource_limit(DWORD resource) {
    if (resource == LINUX_RLIMIT_NOFILE) return JOLT_FDS;
    if (resource == LINUX_RLIMIT_STACK) return STACK_SIZE;
    return LINUX_RLIM_INFINITY;
}

LONG sys_ugetrlimit(const DWORD *arguments) {
    DWORD limit = resource_limit(arguments[0]);
    struct linux_rlimit value = { limit, limit };
    return guest_copy_out(arguments[1], &value, sizeof value) ? 0 : error(LINUX_EFAULT);
}

LONG sys_prlimit64(const DWORD *arguments) {
    DWORD resource = arguments[1], previous = arguments[3];
    if (!previous) return 0;
    DWORD limit = resource_limit(resource);
    ULONGLONG wide = limit == LINUX_RLIM_INFINITY ? ~0ULL : limit;
    struct linux_rlimit64 value = { wide, wide };
    return guest_copy_out(previous, &value, sizeof value) ? 0 : error(LINUX_EFAULT);
}

DWORD next_random(void) {
    state.random = state.random * 1103515245UL + 12345UL + GetTickCount();
    return state.random;
}

LONG sys_getrandom(const DWORD *arguments) {
    DWORD buffer = arguments[0], length = arguments[1];
    if (!guest_writable(buffer, length)) return error(LINUX_EFAULT);
    random_fill((BYTE *)buffer, length);
    return (LONG)length;
}

LONG sys_sched_yield(const DWORD *arguments) {
    (void)arguments;
    Sleep(0);
    return 0;
}

static DWORD timeval_milliseconds(const struct linux_timeval *value) {
    return (DWORD)value->seconds * MILLISECONDS_PER_SECOND + (DWORD)(value->microseconds + 999) / 1000;
}

static void milliseconds_timeval(DWORD milliseconds, struct linux_timeval *value) {
    value->seconds = (LONG)(milliseconds / MILLISECONDS_PER_SECOND);
    value->microseconds = (LONG)(milliseconds % MILLISECONDS_PER_SECOND) * 1000;
}

static void current_timer(struct linux_itimerval *timer) {
    memset(timer, 0, sizeof *timer);
    milliseconds_timeval(state.alarm_interval, &timer->interval);
    if (!state.alarm_deadline) return;
    LONG left = (LONG)(state.alarm_deadline - GetTickCount());
    milliseconds_timeval(left > 0 ? (DWORD)left : 1, &timer->value);
}

LONG sys_setitimer(const DWORD *arguments) {
    DWORD which = arguments[0], value_pointer = arguments[1], previous_pointer = arguments[2];
    if (which != LINUX_ITIMER_REAL) return error(LINUX_EINVAL);
    struct linux_itimerval timer, previous;
    if (value_pointer && !guest_copy_in(&timer, value_pointer, sizeof timer)) return error(LINUX_EFAULT);
    current_timer(&previous);
    if (previous_pointer && !guest_copy_out(previous_pointer, &previous, sizeof previous)) return error(LINUX_EFAULT);
    if (!value_pointer) return 0;
    DWORD delay = timeval_milliseconds(&timer.value);
    state.alarm_interval = timeval_milliseconds(&timer.interval);
    state.alarm_deadline = delay ? GetTickCount() + delay : 0;
    if (state.alarm_deadline == 0 && delay) state.alarm_deadline = 1;
    return 0;
}

LONG sys_getitimer(const DWORD *arguments) {
    if (arguments[0] != LINUX_ITIMER_REAL) return error(LINUX_EINVAL);
    struct linux_itimerval timer;
    current_timer(&timer);
    return guest_copy_out(arguments[1], &timer, sizeof timer) ? 0 : error(LINUX_EFAULT);
}
