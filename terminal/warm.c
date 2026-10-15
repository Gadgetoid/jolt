#include <windows.h>
#include <string.h>
#include "warm.h"
#include "kernel.h"

#define WARM_BUDGET (4 * 1024 * 1024)
#define WARM_LOW_MEMORY (2 * 1024 * 1024)
#define WARM_CHECK_MILLISECONDS 1000
#define NAME_CAPACITY 48

struct held_image {
    HANDLE mapping;
    DWORD key, size;
};

static struct held_image held[JOLT_WARM_IMAGES];
static DWORD last_check;

static int held_count(DWORD *total) {
    int count = 0;
    *total = 0;
    for (int index = 0; index < JOLT_WARM_IMAGES; index++) {
        if (!held[index].mapping) continue;
        count++;
        *total += held[index].size;
    }
    return count;
}

static void drop(struct held_image *image) {
    if (image->mapping) CloseHandle(image->mapping);
    memset(image, 0, sizeof *image);
}

static struct held_image *least_used(void) {
    struct held_image *oldest = NULL;
    LONG oldest_used = 0;
    for (int index = 0; index < JOLT_WARM_IMAGES; index++) {
        struct held_image *image = &held[index];
        if (!image->mapping) continue;
        LONG used = jolt_image_used(image->key);
        if (!oldest || used < oldest_used) {
            oldest = image;
            oldest_used = used;
        }
    }
    return oldest;
}

static int memory_low(void) {
    MEMORYSTATUS status;
    memset(&status, 0, sizeof status);
    status.dwLength = sizeof status;
    GlobalMemoryStatus(&status);
    return status.dwAvailPhys < WARM_LOW_MEMORY;
}

static void evict_over_budget(void) {
    DWORD total;
    while (held_count(&total) && total > WARM_BUDGET) drop(least_used());
}

void warm_hold(DWORD key, DWORD size) {
    for (int index = 0; index < JOLT_WARM_IMAGES; index++)
        if (held[index].mapping && held[index].key == key) return;
    if (size > WARM_BUDGET || memory_low()) return;
    WCHAR name[NAME_CAPACITY];
    jolt_image_name(name, key);
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, size, name);
    if (!mapping) return;
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
        CloseHandle(mapping);
        return;
    }
    struct held_image *slot = NULL;
    for (int index = 0; index < JOLT_WARM_IMAGES && !slot; index++)
        if (!held[index].mapping) slot = &held[index];
    if (!slot) {
        slot = least_used();
        drop(slot);
    }
    slot->mapping = mapping;
    slot->key = key;
    slot->size = size;
    evict_over_budget();
}

void warm_trim(void) {
    DWORD now = GetTickCount(), total;
    if (now - last_check < WARM_CHECK_MILLISECONDS) return;
    last_check = now;
    if (held_count(&total) && memory_low()) drop(least_used());
}

void warm_release(void) {
    for (int index = 0; index < JOLT_WARM_IMAGES; index++) drop(&held[index]);
}
