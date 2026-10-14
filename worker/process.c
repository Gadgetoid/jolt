#include <windows.h>
#include <string.h>
#include "worker.h"
#include "memory.h"
#include "process.h"
#include "syscalls.h"
#include "files.h"
#include "path.h"
#include "abi.h"

#define EXEC_STRING_CAPACITY 16384
#define EXEC_ARGUMENT_LIMIT 255
#define SCRIPT_HEADER_LIMIT 255


struct exec_request {
    char path[JOLT_GUEST_PATH];
    struct guest_image image;
    char strings[EXEC_STRING_CAPACITY];
    DWORD used;
    char *arguments[EXEC_ARGUMENT_LIMIT + 1];
    DWORD argument_count;
    char *environment[EXEC_ARGUMENT_LIMIT + 1];
    DWORD environment_count;
};

static struct exec_request request;

LONG sys_exit(const DWORD *arguments) {
    state.exit_status = LINUX_WAIT_EXITED(arguments[0]);
    state.context.exit_requested = EXIT_PROCESS;
    return 0;
}

LONG sys_fork(const DWORD *arguments) {
    (void)arguments;
    return worker_fork();
}

LONG sys_clone(const DWORD *arguments) {
    DWORD flags = arguments[0], stack = arguments[1];
    int plain_fork = flags == LINUX_SIGCHLD && !stack;
    return plain_fork ? worker_fork() : error(LINUX_EINVAL);
}

static int append_string(char **list, DWORD *count, const char *text) {
    DWORD length = (DWORD)strlen(text) + 1;
    if (*count >= EXEC_ARGUMENT_LIMIT || length > EXEC_STRING_CAPACITY - request.used) return 0;
    list[(*count)++] = memcpy(request.strings + request.used, text, length);
    request.used += length;
    return 1;
}

static LONG append_guest_list(char **list, DWORD *count, DWORD pointer, int skip_first) {
    for (DWORD item = 0; pointer; item++) {
        DWORD string;
        if (!guest_copy_in(&string, pointer + item * sizeof string, sizeof string)) return error(LINUX_EFAULT);
        if (!string) break;
        if (skip_first && item == 0) continue;
        if (*count >= EXEC_ARGUMENT_LIMIT) return error(LINUX_E2BIG);
        char *destination = request.strings + request.used;
        LONG length = guest_string(string, destination, EXEC_STRING_CAPACITY - request.used);
        if (length == error(LINUX_ENAMETOOLONG)) return error(LINUX_E2BIG);
        if (length < 0) return length;
        list[(*count)++] = destination;
        request.used += (DWORD)length + 1;
    }
    list[*count] = NULL;
    return 0;
}

static LONG read_script_header(const char *absolute, char *header, DWORD capacity) {
    DWORD type = 0;
    char opened[JOLT_GUEST_PATH];
    strncpy(opened, absolute, sizeof opened - 1);
    opened[sizeof opened - 1] = 0;
    if (jolt_file_open(opened, LINUX_O_RDONLY, &type) || type != DESCRIPTION_FILE) return error(LINUX_EACCES);
    jolt_lock();
    int index = jolt_description_alloc(DESCRIPTION_FILE, LINUX_O_RDONLY, opened);
    LONG count = index >= 0 ? jolt_file_read(index, header, capacity - 1, 0) : error(LINUX_ENFILE);
    if (index >= 0) jolt_description_release(index);
    jolt_unlock();
    if (index >= 0) jolt_files_drop(index);
    if (count < 0) return count;
    header[count] = 0;
    return count;
}

static int is_blank(char character) {
    return character == ' ' || character == '\t';
}

static int ends_line(char character) {
    return !character || character == '\n' || character == '\r';
}

static LONG parse_interpreter(char *header, char *interpreter, char *option) {
    if (header[0] != '#' || header[1] != '!') return error(LINUX_ENOEXEC);
    char *cursor = header + 2;
    while (is_blank(*cursor)) cursor++;
    char *name = cursor;
    while (!ends_line(*cursor) && !is_blank(*cursor)) cursor++;
    int has_option = is_blank(*cursor);
    *cursor++ = 0;
    option[0] = 0;
    if (has_option) {
        while (is_blank(*cursor)) cursor++;
        char *end = cursor;
        while (!ends_line(*end)) end++;
        while (end > cursor && is_blank(end[-1])) end--;
        *end = 0;
        strcpy(option, cursor);
    }
    struct jolt_node node;
    if (jolt_absolute_path("/", name, interpreter, JOLT_GUEST_PATH)) return error(LINUX_ENOEXEC);
    if (jolt_lookup(interpreter, &node) || (node.kind != NODE_APPLET && node.kind != NODE_FILE)) return error(LINUX_ENOEXEC);
    return 0;
}

static int allows_unmarked(void) {
    for (DWORD index = 0; index < request.environment_count; index++)
        if (!strcmp(request.environment[index], JOLT_ALLOW_UNMARKED)) return 1;
    return 0;
}

static LONG select_image(void) {
    struct jolt_node node;
    LONG result = jolt_lookup(request.path, &node);
    if (result) return result;
    if (node.kind == NODE_APPLET) {
        image_use_shared(&request.image);
        return 0;
    }
    if (node.kind != NODE_FILE) return error(LINUX_EACCES);
    char header[SCRIPT_HEADER_LIMIT + 1];
    LONG count = read_script_header(request.path, header, sizeof header);
    if (count < 0) return count;
    if (!image_is_elf(header, count)) return error(LINUX_ENOEXEC);
    return image_load(request.path, &request.image, allows_unmarked());
}

static LONG prepare_script(char *header, LONG count) {
    char interpreter[JOLT_GUEST_PATH], option[JOLT_GUEST_PATH];
    if (count < 3) return error(LINUX_ENOEXEC);
    LONG result = parse_interpreter(header, interpreter, option);
    if (result) return result;
    int stored = append_string(request.arguments, &request.argument_count, interpreter) &&
        (!option[0] || append_string(request.arguments, &request.argument_count, option)) &&
        append_string(request.arguments, &request.argument_count, request.path);
    if (!stored) return error(LINUX_E2BIG);
    strcpy(request.path, interpreter);
    return 0;
}

static LONG prepare_exec(DWORD directory, DWORD path, DWORD arguments, DWORD environment) {
    memset(&request, 0, sizeof request);
    LONG result = resolve_at(directory, path, request.path, NULL);
    if (result) return result;
    struct jolt_node node;
    result = jolt_lookup(request.path, &node);
    if (result) return result;
    if (node.kind != NODE_FILE && node.kind != NODE_APPLET) return error(LINUX_EACCES);
    if (node.kind == NODE_FILE) {
        char header[SCRIPT_HEADER_LIMIT + 1];
        LONG count = read_script_header(request.path, header, sizeof header);
        if (count < 0) return count;
        if (!image_is_elf(header, count)) result = prepare_script(header, count);
    }
    if (!result) result = append_guest_list(request.environment, &request.environment_count, environment, 0);
    if (!result) result = select_image();
    if (result) return result;
    int script = request.argument_count > 0;
    result = append_guest_list(request.arguments, &request.argument_count, arguments, script);
    if (result) return result;
    if (!request.argument_count && !append_string(request.arguments, &request.argument_count, request.path))
        return error(LINUX_E2BIG);
    request.arguments[request.argument_count] = NULL;
    return 0;
}

static LONG exec_at(DWORD directory, DWORD path, DWORD arguments, DWORD environment) {
    LONG result = prepare_exec(directory, path, arguments, environment);
    if (result) {
        image_release(&request.image);
        return result;
    }
    int loaded = worker_exec(request.path, request.arguments, request.environment, &request.image) == 0;
    image_release(&request.image);
    state.exit_status = loaded ? state.exit_status : LINUX_SIGSEGV;
    state.context.exit_requested = loaded ? EXIT_EXEC : EXIT_PROCESS;
    return 0;
}

LONG sys_execve(const DWORD *arguments) {
    return exec_at(LINUX_AT_FDCWD, arguments[0], arguments[1], arguments[2]);
}

LONG sys_execveat(const DWORD *arguments) {
    return exec_at(arguments[0], arguments[1], arguments[2], arguments[3]);
}

static int child_matches(const struct jolt_process *child, LONG pid) {
    if (pid > 0) return child->pid == pid;
    if (pid == 0) return child->pgid == self->pgid;
    if (pid < -1) return child->pgid == -pid;
    return 1;
}

enum child_report { REPORT_NONE, REPORT_EXITED, REPORT_STOPPED, REPORT_CONTINUED };

static enum child_report reportable(const struct jolt_process *child, DWORD options) {
    if (child->state == PROCESS_ZOMBIE) return REPORT_EXITED;
    if ((options & LINUX_WUNTRACED) && child->stopped && !child->stop_reported) return REPORT_STOPPED;
    if ((options & LINUX_WCONTINUED) && child->continued) return REPORT_CONTINUED;
    return REPORT_NONE;
}

static int find_child(LONG pid, DWORD options, enum child_report *report) {
    int found = -1;
    *report = REPORT_NONE;
    for (int index = 0; index < JOLT_PROCESSES; index++) {
        const struct jolt_process *child = &kernel()->processes[index];
        if (child->state == PROCESS_FREE || index == slot || child->ppid != self->pid) continue;
        if (!child_matches(child, pid)) continue;
        found = index;
        *report = reportable(child, options);
        if (*report != REPORT_NONE) return index;
    }
    return found;
}

static DWORD collect_report(struct jolt_process *child, enum child_report report) {
    switch (report) {
    case REPORT_STOPPED:
        child->stop_reported = 1;
        return LINUX_WAIT_STOPPED(child->stop_signal);
    case REPORT_CONTINUED:
        child->continued = 0;
        return LINUX_WAIT_CONTINUED;
    default:
        child->state = PROCESS_FREE;
        return child->status;
    }
}

LONG sys_wait4(const DWORD *arguments) {
    LONG pid = (LONG)arguments[0];
    DWORD status_pointer = arguments[1], options = arguments[2], usage = arguments[3];
    if (status_pointer && !guest_writable(status_pointer, sizeof(DWORD))) return error(LINUX_EFAULT);
    if (usage && !guest_writable(usage, LINUX_RUSAGE_SIZE)) return error(LINUX_EFAULT);
    for (;;) {
        enum child_report report;
        jolt_lock();
        int index = find_child(pid, options, &report);
        LONG result = 1;
        if (index < 0) result = error(LINUX_ECHILD);
        else if (report == REPORT_NONE && (options & LINUX_WNOHANG)) result = 0;
        else if (report == REPORT_NONE && signal_ready()) result = error(LINUX_EINTR);
        if (index >= 0 && report != REPORT_NONE) {
            struct jolt_process *child = &kernel()->processes[index];
            LONG reaped = child->pid;
            DWORD status = collect_report(child, report);
            self->waiting = WAIT_NONE;
            jolt_unlock();
            if (status_pointer) guest_copy_out(status_pointer, &status, sizeof status);
            if (usage) memset((void *)usage, 0, LINUX_RUSAGE_SIZE);
            return reaped;
        }
        if (result <= 0) {
            self->waiting = WAIT_NONE;
            jolt_unlock();
            return result;
        }
        self->waiting = WAIT_CHILD;
        jolt_unlock();
        block_until(INFINITE);
    }
}

LONG sys_getpid(const DWORD *arguments) {
    (void)arguments;
    return self->pid;
}

LONG sys_getppid(const DWORD *arguments) {
    (void)arguments;
    return self->ppid;
}

LONG sys_getpgid(const DWORD *arguments) {
    LONG pid = (LONG)arguments[0];
    if (!pid || pid == self->pid) return self->pgid;
    int index = jolt_find_pid(pid);
    return index < 0 ? error(LINUX_ESRCH) : kernel()->processes[index].pgid;
}

LONG sys_getpgrp(const DWORD *arguments) {
    (void)arguments;
    return self->pgid;
}

LONG sys_getsid(const DWORD *arguments) {
    (void)arguments;
    return self->sid;
}

LONG sys_setpgid(const DWORD *arguments) {
    LONG pid = arguments[0] ? (LONG)arguments[0] : self->pid;
    LONG group = arguments[1] ? (LONG)arguments[1] : pid;
    jolt_lock();
    int index = jolt_find_pid(pid);
    if (index >= 0) kernel()->processes[index].pgid = group;
    jolt_unlock();
    return index < 0 ? error(LINUX_ESRCH) : 0;
}

LONG sys_setsid(const DWORD *arguments) {
    (void)arguments;
    jolt_lock();
    self->sid = self->pgid = self->pid;
    self->ctty = JOLT_NO_TTY;
    jolt_unlock();
    return self->pid;
}

LONG sys_get_user_ids(const DWORD *arguments) {
    DWORD zero = 0;
    for (int index = 0; index < 3; index++)
        if (!guest_copy_out(arguments[index], &zero, sizeof zero)) return error(LINUX_EFAULT);
    return 0;
}
