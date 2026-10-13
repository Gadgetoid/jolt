extern uintptr_t __jolt_thread_pointer;

static inline uintptr_t __get_tp(void) {
    return __jolt_thread_pointer;
}

#define TLS_ABOVE_TP
#define GAP_ABOVE_TP 8
#define MC_PC arm_pc