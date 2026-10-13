#include "path.h"
#include <string.h>

int jolt_resolve_path(const char *guest, char *relative, size_t capacity) {
    if (!guest || !relative || !capacity) return -22;
    relative[0] = 0;
    if (!guest[0]) return -2;
    size_t length = 0;
    while (*guest) {
        while (*guest == '/') guest++;
        const char *component = guest;
        while (*guest && *guest != '/') {
            unsigned char character = (unsigned char)*guest++;
            if (character >= 128) return -84;
            if (character < 32 || strchr("\\:*?\"<>|", character)) return -13;
        }
        size_t count = (size_t)(guest - component);
        if (!count || (count == 1 && component[0] == '.')) continue;
        if (count == 2 && component[0] == '.' && component[1] == '.') {
            if (!length) return -13;
            while (length && relative[length - 1] != '\\') length--;
            if (length) length--;
            relative[length] = 0;
            continue;
        }
        if (component[count - 1] == '.' || component[count - 1] == ' ') return -13;
        size_t separator = length != 0;
        if (count >= capacity || length + separator >= capacity - count) return -36;
        if (separator) relative[length++] = '\\';
        memcpy(relative + length, component, count);
        length += count;
        relative[length] = 0;
    }
    return 0;
}
static int append_components(const char *guest, char *absolute, size_t capacity, size_t *length) {
    while (*guest) {
        while (*guest == '/') guest++;
        const char *component = guest;
        while (*guest && *guest != '/') {
            unsigned char character = (unsigned char)*guest++;
            if (character >= 128) return -84;
            if (character < 32 || strchr("\\:*?\"<>|", character)) return -2;
        }
        size_t count = (size_t)(guest - component);
        if (!count || (count == 1 && component[0] == '.')) continue;
        if (count == 2 && component[0] == '.' && component[1] == '.') {
            while (*length > 1 && absolute[*length - 1] != '/') (*length)--;
            if (*length > 1) (*length)--;
            absolute[*length] = 0;
            continue;
        }
        if (component[count - 1] == '.' || component[count - 1] == ' ') return -2;
        size_t separator = *length > 1;
        if (*length + separator + count >= capacity) return -36;
        if (separator) absolute[(*length)++] = '/';
        memcpy(absolute + *length, component, count);
        *length += count;
        absolute[*length] = 0;
    }
    return 0;
}

int jolt_absolute_path(const char *base, const char *guest, char *absolute, size_t capacity) {
    if (!guest || !absolute || capacity < 2) return -22;
    if (!guest[0]) return -2;
    size_t length = 1;
    absolute[0] = '/';
    absolute[1] = 0;
    if (guest[0] != '/' && base) {
        int result = append_components(base, absolute, capacity, &length);
        if (result) return result;
    }
    return append_components(guest, absolute, capacity, &length);
}
