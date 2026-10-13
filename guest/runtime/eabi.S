.syntax unified
.arch armv4
.arm
.text
.global __aeabi_uidivmod
.type __aeabi_uidivmod, %function
__aeabi_uidivmod:
    push {r4, lr}
    sub sp, sp, #8
    mov r2, sp
    bl __udivmodsi4
    ldr r1, [sp]
    add sp, sp, #8
    pop {r4, pc}

.global __aeabi_idivmod
.type __aeabi_idivmod, %function
__aeabi_idivmod:
    push {r4, lr}
    sub sp, sp, #8
    mov r2, sp
    bl __divmodsi4
    ldr r1, [sp]
    add sp, sp, #8
    pop {r4, pc}

.global __aeabi_uldivmod
.type __aeabi_uldivmod, %function
__aeabi_uldivmod:
    push {r4, lr}
    sub sp, sp, #16
    add r4, sp, #8
    str r4, [sp]
    bl __udivmoddi4
    ldr r2, [sp, #8]
    ldr r3, [sp, #12]
    add sp, sp, #16
    pop {r4, pc}

.global __aeabi_ldivmod
.type __aeabi_ldivmod, %function
__aeabi_ldivmod:
    push {r4, lr}
    sub sp, sp, #16
    add r4, sp, #8
    str r4, [sp]
    bl __divmoddi4
    ldr r2, [sp, #8]
    ldr r3, [sp, #12]
    add sp, sp, #16
    pop {r4, pc}