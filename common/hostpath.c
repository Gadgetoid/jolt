#include <string.h>
#include <wchar.h>
#include "hostpath.h"
#include "kernel.h"

static int covers(const char *target, const char *absolute, size_t *length) {
    size_t count = strlen(target);
    if (!count || strncmp(target, absolute, count)) return 0;
    if (absolute[count] && absolute[count] != '/') return 0;
    *length = count;
    return 1;
}

int jolt_find_mount(const char *absolute, size_t *matched) {
    int best = -1;
    size_t best_length = 0;
    for (int index = 0; index < JOLT_MOUNTS; index++) {
        size_t length;
        if (covers(jolt.kernel->mounts[index].target, absolute, &length) && length > best_length) {
            best = index;
            best_length = length;
        }
    }
    if (matched) *matched = best_length;
    return best;
}

int jolt_is_mount_point(const char *absolute) {
    size_t matched;
    return jolt_find_mount(absolute, &matched) >= 0 && !absolute[matched];
}

static LONG append(WCHAR *path, size_t *used, const char *text) {
    for (; *text; text++) {
        if (*used >= MAX_PATH - 1) return -(LONG)LINUX_ENAMETOOLONG;
        path[(*used)++] = *text == '/' ? L'\\' : (unsigned char)*text;
    }
    path[*used] = 0;
    return 0;
}

LONG jolt_host_path(const WCHAR *root, const char *absolute, WCHAR *path) {
    if (absolute[0] != '/') return -(LONG)LINUX_ENAMETOOLONG;
    size_t matched;
    int mount = jolt_find_mount(absolute, &matched);
    size_t used = 0;
    path[0] = 0;
    if (mount < 0) {
        used = wcslen(root);
        if (used >= MAX_PATH) return -(LONG)LINUX_ENAMETOOLONG;
        wcscpy(path, root);
        return absolute[1] ? append(path, &used, absolute) : 0;
    }
    LONG result = append(path, &used, jolt.kernel->mounts[mount].source);
    if (!result) result = append(path, &used, absolute + matched);
    if (!result && !used) wcscpy(path, L"\\");
    return result;
}
