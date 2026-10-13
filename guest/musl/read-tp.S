.syntax unified
.arch armv4
.arm
.text
.global __aeabi_read_tp
.hidden __aeabi_read_tp
.type __aeabi_read_tp, %function
__aeabi_read_tp:
    ldr r0, =__jolt_thread_pointer
    ldr r0, [r0]
    mov pc, lr