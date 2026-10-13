.syntax unified
.arch armv4
.arm
.text
.global jolt_enter
.type jolt_enter, %function
jolt_enter:
    push {r4-r11, lr}
    str sp, [r2]
    mov r12, r0
    mov sp, r1
    mov r0, #0
    mov r1, #0
    mov r2, #0
    mov r3, #0
    mov r4, #0
    mov r5, #0
    mov r6, #0
    mov r7, #0
    mov r8, #0
    mov r9, #0
    mov r10, #0
    mov r11, #0
    mov lr, #0
    mov pc, r12
.size jolt_enter, . - jolt_enter

.global jolt_resume
.type jolt_resume, %function
jolt_resume:
    push {r4-r11, lr}
    str sp, [r0]
    ldr r1, [r0, #8]
    ldr sp, [r0, #4]
    msr cpsr_f, r1
    pop {r0-r12, pc}
.size jolt_resume, . - jolt_resume

.global jolt_gate
.type jolt_gate, %function
jolt_gate:
    push {r0-r12, lr}
    str sp, [r12, #4]
    mrs r0, cpsr
    str r0, [r12, #8]
    mov r1, sp
    ldr sp, [r12]
    sub sp, sp, #16
    bic sp, sp, #7
    str r12, [sp]
    mov r0, r12
    bl jolt_dispatch
    ldr r12, [sp]
    ldr r1, [r12, #12]
    cmp r1, #0
    bne guest_exit
    ldr r1, [r12, #8]
    ldr sp, [r12, #4]
    str r0, [sp]
    msr cpsr_f, r1
    pop {r0-r12, pc}
guest_exit:
    ldr r0, [r12, #16]
    ldr sp, [r12]
    pop {r4-r11, pc}
.size jolt_gate, . - jolt_gate

.section .note.GNU-stack,"",%progbits