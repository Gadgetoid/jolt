#include <stddef.h>
#include <stdint.h>

uintptr_t __jolt_gate;
int main(int count, char **arguments, char **environment);
int __libc_start_main(int (*entry)(int, char **, char **), int count, char **arguments,
    void (*initialize)(void), void (*finalize)(void), void (*loader_finalize)(void));

void _init(void) {}
void _fini(void) {}

void __jolt_start(uintptr_t *startup) {
    uintptr_t *auxiliary = startup + startup[0] + 2;
    while (*auxiliary) auxiliary++;
    auxiliary++;
    while (auxiliary[0]) {
        if (auxiliary[0] == 0x70000001) {
            const uintptr_t *descriptor = (const uintptr_t *)auxiliary[1];
            if (descriptor[0] == 0x4a4f4c54 && descriptor[1] == 1) __jolt_gate = descriptor[2];
        }
        auxiliary += 2;
    }
    if (!__jolt_gate) for (;;) {}
    __libc_start_main(main, (int)startup[0], (char **)(startup + 1), _init, _fini, NULL);
    for (;;) {}
}