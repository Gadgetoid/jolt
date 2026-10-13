.syntax unified
.arch armv4
.arm
.text
.global setjmp
.global _setjmp
.type setjmp, %function
.type _setjmp, %function
setjmp:
_setjmp:
    stmia r0, {r4-r11, sp, lr}
    mov r0, #0
    mov pc, lr

.global longjmp
.global _longjmp
.type longjmp, %function
.type _longjmp, %function
longjmp:
_longjmp:
    mov r12, r0
    movs r0, r1
    moveq r0, #1
    ldmia r12, {r4-r11, sp, lr}
    mov pc, lr
