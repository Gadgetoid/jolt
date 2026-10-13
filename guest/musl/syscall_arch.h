#define __SYSCALL_LL_E(value) ((union { long long wide; long words[2]; }){ .wide = value }).words[0], ((union { long long wide; long words[2]; }){ .wide = value }).words[1]
#define __SYSCALL_LL_O(value) 0, __SYSCALL_LL_E(value)

long __jolt_syscall(long number, long first, long second, long third, long fourth, long fifth, long sixth);

static inline long __syscall0(long number) {
    return __jolt_syscall(number, 0, 0, 0, 0, 0, 0);
}
static inline long __syscall1(long number, long first) {
    return __jolt_syscall(number, first, 0, 0, 0, 0, 0);
}
static inline long __syscall2(long number, long first, long second) {
    return __jolt_syscall(number, first, second, 0, 0, 0, 0);
}
static inline long __syscall3(long number, long first, long second, long third) {
    return __jolt_syscall(number, first, second, third, 0, 0, 0);
}
static inline long __syscall4(long number, long first, long second, long third, long fourth) {
    return __jolt_syscall(number, first, second, third, fourth, 0, 0);
}
static inline long __syscall5(long number, long first, long second, long third, long fourth, long fifth) {
    return __jolt_syscall(number, first, second, third, fourth, fifth, 0);
}
static inline long __syscall6(long number, long first, long second, long third, long fourth, long fifth, long sixth) {
    return __jolt_syscall(number, first, second, third, fourth, fifth, sixth);
}

#define SYSCALL_FADVISE_6_ARG
#define SYSCALL_IPC_BROKEN_MODE