#include <stdint.h>
#include <string.h>

int __eqdf2(double first, double second);
int __ltdf2(double first, double second);
int __gtdf2(double first, double second);
int __eqsf2(float first, float second);
int __ltsf2(float first, float second);
int __gtsf2(float first, float second);

int __aeabi_dcmpeq(double first, double second) { return __eqdf2(first, second) == 0; }
int __aeabi_dcmplt(double first, double second) { return __ltdf2(first, second) < 0; }
int __aeabi_dcmple(double first, double second) { return __ltdf2(first, second) <= 0; }
int __aeabi_dcmpgt(double first, double second) { return __gtdf2(first, second) > 0; }
int __aeabi_dcmpge(double first, double second) { return __gtdf2(first, second) >= 0; }
int __aeabi_fcmpeq(float first, float second) { return __eqsf2(first, second) == 0; }
int __aeabi_fcmplt(float first, float second) { return __ltsf2(first, second) < 0; }
int __aeabi_fcmple(float first, float second) { return __ltsf2(first, second) <= 0; }
int __aeabi_fcmpgt(float first, float second) { return __gtsf2(first, second) > 0; }
int __aeabi_fcmpge(float first, float second) { return __gtsf2(first, second) >= 0; }

void __aeabi_uwrite8(uint64_t value, void *pointer) { memcpy(pointer, &value, sizeof value); }
uint64_t __aeabi_uread8(const void *pointer) {
    uint64_t value;
    memcpy(&value, pointer, sizeof value);
    return value;
}
uint32_t __aeabi_uread4(const void *pointer) {
    const uint8_t *bytes = pointer;
    return bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

void __aeabi_uwrite4(uint32_t value, void *pointer) {
    uint8_t *bytes = pointer;
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}
