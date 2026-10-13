.syntax unified
.arch armv4
.arm

.global __restore
.hidden __restore
.type __restore,%function
__restore:
	mov r0,#119
	bl __jolt_syscall

.global __restore_rt
.hidden __restore_rt
.type __restore_rt,%function
__restore_rt:
	mov r0,#173
	bl __jolt_syscall
