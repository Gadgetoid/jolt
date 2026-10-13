#include <windows.h>
#include <string.h>
#include "worker.h"
#include "memory.h"
#include "process.h"
#include "syscalls.h"

#define GATE_TRAMPOLINE (GATE_PAGE + 16)
#define REGISTER_GATE 12
#define BRANCH_TO_GATE_OFFSET 8
#define SIGNAL_LIMIT_ACCEPTED 64

struct signal_frame {
    struct jolt_registers registers;
    DWORD restorer;
    DWORD handler;
};

_Static_assert(sizeof(struct jolt_registers) == 56, "Gate register frame size");

void update_signal_masks(void) {
    DWORD fatal = 0, ignored = 0;
    for (int signal = 1; signal <= LINUX_SIGNAL_LIMIT; signal++) {
        DWORD bit = LINUX_SIGNAL_BIT(signal), handler = state.actions[signal].handler;
        int default_action = handler == LINUX_SIG_DFL;
        if (handler == LINUX_SIG_IGN || (default_action && (LINUX_DEFAULT_IGNORED & bit))) ignored |= bit;
        else if (default_action && !(LINUX_DEFAULT_STOP & bit)) fatal |= bit;
    }
    fatal |= LINUX_SIGNAL_BIT(LINUX_SIGKILL);
    ignored &= ~LINUX_SIGNAL_BIT(LINUX_SIGKILL);
    jolt_lock();
    self->fatal = fatal;
    self->ignored = ignored;
    self->pending &= ~ignored;
    jolt_unlock();
}

void suspend_signals(DWORD mask) {
    state.suspend_saved = state.blocked;
    state.suspended = 1;
    state.blocked = mask & ~LINUX_UNBLOCKABLE;
}

void resume_signals(void) {
    if (!state.suspended) return;
    state.blocked = state.suspend_saved;
    state.suspended = 0;
}

LONG wait_for_signal(void) {
    for (;;) {
        jolt_lock();
        int ready = signal_ready();
        self->waiting = ready ? WAIT_NONE : WAIT_SIGNAL;
        jolt_unlock();
        if (ready) return error(LINUX_EINTR);
        block_until(INFINITE);
    }
}

static int kill_matches(const struct jolt_process *process, int index, LONG pid) {
    if (pid > 0) return process->pid == pid;
    if (pid == 0) return process->pgid == self->pgid;
    if (pid == -1) return process->pid != 1 && index != slot;
    return process->pgid == -pid;
}

static LONG send_signal(LONG pid, DWORD signal) {
    if (signal > SIGNAL_LIMIT_ACCEPTED) return error(LINUX_EINVAL);
    if (signal > LINUX_SIGNAL_LIMIT) return 0;
    int found = 0;
    jolt_lock();
    for (int index = 0; index < JOLT_PROCESSES; index++) {
        const struct jolt_process *process = &kernel()->processes[index];
        if (process->state == PROCESS_FREE || !kill_matches(process, index, pid)) continue;
        found = 1;
        if (signal && process->state != PROCESS_ZOMBIE) jolt_signal_process(index, (int)signal);
    }
    jolt_unlock();
    return found ? 0 : error(LINUX_ESRCH);
}

LONG sys_kill(const DWORD *arguments) {
    return send_signal((LONG)arguments[0], arguments[1]);
}

LONG sys_tkill(const DWORD *arguments) {
    if ((LONG)arguments[0] != self->pid) return error(LINUX_ESRCH);
    return send_signal(self->pid, arguments[1]);
}

LONG sys_tgkill(const DWORD *arguments) {
    if ((LONG)arguments[1] != self->pid) return error(LINUX_ESRCH);
    return send_signal(self->pid, arguments[2]);
}

LONG sys_rt_sigaction(const DWORD *arguments) {
    DWORD signal = arguments[0], action_pointer = arguments[1], previous_pointer = arguments[2], size = arguments[3];
    if (signal < 1 || signal > SIGNAL_LIMIT_ACCEPTED || size != LINUX_SIGSET_SIZE) return error(LINUX_EINVAL);
    struct linux_sigaction action, previous;
    if (action_pointer && !guest_copy_in(&action, action_pointer, sizeof action)) return error(LINUX_EFAULT);
    if (previous_pointer && !guest_writable(previous_pointer, sizeof previous)) return error(LINUX_EFAULT);
    memset(&previous, 0, sizeof previous);
    if (signal <= LINUX_SIGNAL_LIMIT) {
        const struct signal_action *current = &state.actions[signal];
        previous.handler = current->handler;
        previous.flags = current->flags;
        previous.restorer = current->restorer;
        previous.mask[0] = current->mask;
    }
    if (previous_pointer) guest_copy_out(previous_pointer, &previous, sizeof previous);
    if (!action_pointer || signal > LINUX_SIGNAL_LIMIT) return 0;
    if (LINUX_SIGNAL_BIT(signal) & LINUX_UNBLOCKABLE) return error(LINUX_EINVAL);
    state.actions[signal] = (struct signal_action){ action.handler, action.flags, action.restorer, action.mask[0] };
    update_signal_masks();
    return 0;
}

LONG sys_rt_sigprocmask(const DWORD *arguments) {
    DWORD how = arguments[0], set_pointer = arguments[1], previous_pointer = arguments[2], size = arguments[3];
    if (size != LINUX_SIGSET_SIZE) return error(LINUX_EINVAL);
    DWORD set[2] = { 0, 0 }, previous[2] = { state.blocked, 0 };
    if (set_pointer && !guest_copy_in(set, set_pointer, sizeof set)) return error(LINUX_EFAULT);
    if (previous_pointer && !guest_writable(previous_pointer, sizeof previous)) return error(LINUX_EFAULT);
    if (set_pointer) {
        if (how == LINUX_SIG_BLOCK) state.blocked |= set[0];
        else if (how == LINUX_SIG_UNBLOCK) state.blocked &= ~set[0];
        else if (how == LINUX_SIG_SETMASK) state.blocked = set[0];
        else return error(LINUX_EINVAL);
        state.blocked &= ~LINUX_UNBLOCKABLE;
    }
    if (previous_pointer) guest_copy_out(previous_pointer, previous, sizeof previous);
    return 0;
}

LONG sys_rt_sigpending(const DWORD *arguments) {
    DWORD pending[2] = { self->pending & state.blocked, 0 };
    return guest_copy_out(arguments[0], pending, sizeof pending) ? 0 : error(LINUX_EFAULT);
}

LONG sys_rt_sigsuspend(const DWORD *arguments) {
    DWORD set_pointer = arguments[0], size = arguments[1], mask;
    if (size != LINUX_SIGSET_SIZE) return error(LINUX_EINVAL);
    if (!guest_copy_in(&mask, set_pointer, sizeof mask)) return error(LINUX_EFAULT);
    suspend_signals(mask);
    return wait_for_signal();
}

LONG sys_pause(const DWORD *arguments) {
    (void)arguments;
    return wait_for_signal();
}

static void terminate_with(struct jolt_context *context, DWORD signal) {
    state.exit_status = signal;
    context->exit_requested = EXIT_PROCESS;
}

static int take_signal(void) {
    jolt_lock();
    check_alarm();
    DWORD ready = self->pending & ~state.blocked;
    int signal = 0;
    if (ready) {
        signal = 1;
        while (!(ready & LINUX_SIGNAL_BIT(signal))) signal++;
        self->pending &= ~LINUX_SIGNAL_BIT(signal);
        if (!(self->pending & self->fatal)) self->signal_time = 0;
    }
    jolt_unlock();
    return signal;
}

static int restartable(DWORD number) {
    switch (number) {
    case SYS_rt_sigsuspend: case SYS_pause: case SYS_nanosleep: case SYS_clock_nanosleep:
    case SYS_clock_nanosleep_time64: case SYS_poll: case SYS_ppoll: case SYS_ppoll_time64:
    case SYS_newselect: case SYS_pselect6: case SYS_pselect6_time64:
        return 0;
    default:
        return 1;
    }
}

static int restartable_after_stop(DWORD number) {
    return number != SYS_rt_sigsuspend && number != SYS_pause;
}

static int save_interrupted(struct jolt_context *context, const struct signal_action *action, LONG result,
    DWORD original, DWORD number) {
    if (state.saved_count == MAX_SAVED_SIGNALS) return 0;
    struct saved_signal *saved = &state.saved[state.saved_count++];
    saved->frame = context->guest_frame;
    memcpy(&saved->registers, (const void *)context->guest_frame, sizeof saved->registers);
    saved->registers.values[0] = (DWORD)result;
    saved->flags = context->guest_flags;
    saved->blocked = state.suspended ? state.suspend_saved : state.blocked;
    state.suspended = 0;
    if (result == error(LINUX_EINTR) && (action->flags & LINUX_SA_RESTART) && restartable(number)) {
        saved->registers.values[0] = original;
        saved->registers.values[REGISTER_GATE] = GATE_PAGE;
        saved->registers.return_address -= BRANCH_TO_GATE_OFFSET;
    }
    return 1;
}

static LONG enter_handler(struct jolt_context *context, int signal, LONG result, DWORD original, DWORD number) {
    struct signal_action *action = &state.actions[signal];
    DWORD frame = context->guest_frame;
    DWORD information = (frame - LINUX_SIGINFO_SIZE) & ~7UL;
    DWORD handler_frame = information - sizeof(struct signal_frame);
    if (!guest_writable(handler_frame, frame + sizeof(struct jolt_registers) - handler_frame) ||
        !save_interrupted(context, action, result, original, number)) {
        terminate_with(context, LINUX_SIGSEGV);
        return result;
    }
    memset((void *)information, 0, LINUX_SIGINFO_SIZE);
    *(DWORD *)information = (DWORD)signal;
    struct signal_frame *entry = (struct signal_frame *)handler_frame;
    memset(entry, 0, sizeof *entry);
    entry->registers.values[0] = (DWORD)signal;
    entry->registers.values[1] = information;
    entry->registers.return_address = GATE_TRAMPOLINE;
    entry->restorer = action->restorer;
    entry->handler = action->handler;
    state.blocked = state.saved[state.saved_count - 1].blocked | action->mask;
    if (!(action->flags & LINUX_SA_NODEFER)) state.blocked |= LINUX_SIGNAL_BIT(signal);
    state.blocked &= ~LINUX_UNBLOCKABLE;
    if (action->flags & LINUX_SA_RESETHAND) {
        action->handler = LINUX_SIG_DFL;
        update_signal_masks();
    }
    context->guest_frame = handler_frame;
    trace("signal %d handler %08lx\r\n", signal, entry->handler);
    return signal;
}

static void stop_process(int signal) {
    jolt_lock();
    self->stopped = 1;
    self->stop_signal = (DWORD)signal;
    self->stop_reported = 0;
    self->continued = 0;
    jolt_notify_parent(slot);
    jolt_unlock();
    trace("stopped by signal %d\r\n", signal);
    for (;;) {
        jolt_lock();
        int stopped = self->stopped != 0;
        int killed = (self->pending & LINUX_SIGNAL_BIT(LINUX_SIGKILL)) != 0;
        self->waiting = stopped && !killed ? WAIT_STOPPED : WAIT_NONE;
        jolt_unlock();
        if (!stopped || killed) return;
        block_until(INFINITE);
    }
}

static LONG restart_syscall(struct jolt_context *context, DWORD original) {
    struct jolt_registers *frame = (struct jolt_registers *)context->guest_frame;
    frame->values[REGISTER_GATE] = GATE_PAGE;
    frame->return_address -= BRANCH_TO_GATE_OFFSET;
    return (LONG)original;
}

LONG deliver_signals(struct jolt_context *context, LONG result, DWORD original, DWORD number) {
    int resumed = 0;
    for (;;) {
        int signal = take_signal();
        if (!signal) {
            resume_signals();
            if (resumed && result == error(LINUX_EINTR) && restartable_after_stop(number)) return restart_syscall(context, original);
            return result;
        }
        DWORD bit = LINUX_SIGNAL_BIT(signal), handler = state.actions[signal].handler;
        if (handler == LINUX_SIG_IGN) continue;
        if (handler == LINUX_SIG_DFL && (bit & LINUX_DEFAULT_STOP)) {
            stop_process(signal);
            resumed = 1;
            continue;
        }
        if (handler == LINUX_SIG_DFL && (bit & LINUX_DEFAULT_IGNORED)) continue;
        if (handler == LINUX_SIG_DFL) {
            terminate_with(context, (DWORD)signal);
            return result;
        }
        return enter_handler(context, signal, result, original, number);
    }
}

LONG sys_rt_sigreturn(const DWORD *arguments) {
    (void)arguments;
    struct jolt_context *context = &state.context;
    if (!state.saved_count) {
        terminate_with(context, LINUX_SIGSEGV);
        return 0;
    }
    struct saved_signal *saved = &state.saved[--state.saved_count];
    if (!guest_copy_out(saved->frame, &saved->registers, sizeof saved->registers)) {
        terminate_with(context, LINUX_SIGSEGV);
        return 0;
    }
    context->guest_frame = saved->frame;
    context->guest_flags = saved->flags;
    state.blocked = saved->blocked;
    return (LONG)saved->registers.values[0];
}
