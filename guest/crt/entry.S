#include "abi.h"
.syntax unified
.arch armv4
.arm
.text
.global _start
.type _start, %function
_start:
    mov r0, sp
    b __jolt_start

.global __jolt_syscall
.type __jolt_syscall, %function
__jolt_syscall:
    push {r4, r5, r7, lr}
    mov r7, r0
    mov r0, r1
    mov r1, r2
    mov r2, r3
    ldr r3, [sp, #16]
    ldr r4, [sp, #20]
    ldr r5, [sp, #24]
    ldr r12, =__jolt_gate
    ldr r12, [r12]
    mov lr, pc
    mov pc, r12
    pop {r4, r5, r7, pc}
.section .note.jolt, "a", %note
.balign 4
.long note_name_end - note_name
.long note_description_end - note_description
.long JOLT_NOTE_ABI
note_name:
.asciz JOLT_NOTE_NAME
note_name_end:
.balign 4
note_description:
.long JOLT_ABI_VERSION
note_description_end:
