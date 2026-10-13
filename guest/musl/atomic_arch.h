#define a_cas a_cas
static inline int a_cas(volatile int *pointer, int expected, int replacement) {
    int previous = *pointer;
    if (previous == expected) *pointer = replacement;
    __asm__ __volatile__("" : : : "memory");
    return previous;
}

#define a_barrier a_barrier
static inline void a_barrier(void) {
    __asm__ __volatile__("" : : : "memory");
}

#define a_crash a_crash
static inline void a_crash(void) {
    __asm__ __volatile__(".word 0xe7f000f0" : : : "memory");
}