#include <stdint.h>

uintptr_t __jolt_thread_pointer;

int __set_thread_area(void *pointer) {
    __jolt_thread_pointer = (uintptr_t)pointer;
    return 1;
}