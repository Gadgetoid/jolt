#pragma once
#include <windows.h>

struct jolt_registers {
    DWORD values[13];
    DWORD return_address;
};

struct jolt_context {
    DWORD host_stack;
    DWORD guest_frame;
    DWORD guest_flags;
    DWORD exit_requested;
    DWORD exit_code;
    DWORD syscall_count;
};

DWORD jolt_enter(DWORD entry, DWORD stack, struct jolt_context *context);
DWORD jolt_resume(struct jolt_context *context);
void jolt_gate(void);
LONG jolt_dispatch(struct jolt_context *context, struct jolt_registers *registers);
