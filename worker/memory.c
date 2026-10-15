#include <windows.h>
#include <string.h>
#include "memory.h"
#include "syscalls.h"
#include "optional.h"

DWORD page_round_up(DWORD value) {
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static DWORD page_round_down(DWORD value) {
    return value & ~(PAGE_SIZE - 1);
}

static int range_allows(DWORD pointer, DWORD length, DWORD permission) {
    if (!length) return 1;
    if (pointer < GUEST_START || pointer >= GUEST_LIMIT || length > GUEST_LIMIT - pointer) return 0;
    DWORD position = pointer, end = pointer + length;
    while (position < end) {
        const struct guest_range *covering = NULL;
        for (DWORD index = 0; index < state.range_count && !covering; index++) {
            const struct guest_range *range = &state.ranges[index];
            if (position >= range->start && position < range->end && (range->flags & permission)) covering = range;
        }
        if (!covering) return 0;
        position = covering->end;
    }
    return 1;
}

int guest_readable(DWORD pointer, DWORD length) {
    return range_allows(pointer, length, RANGE_READ);
}

int guest_writable(DWORD pointer, DWORD length) {
    return range_allows(pointer, length, RANGE_WRITE);
}

int guest_copy_in(void *destination, DWORD source, DWORD length) {
    if (!guest_readable(source, length)) return 0;
    memcpy(destination, (const void *)source, length);
    return 1;
}

int guest_copy_out(DWORD destination, const void *source, DWORD length) {
    if (!guest_writable(destination, length)) return 0;
    memcpy((void *)destination, source, length);
    return 1;
}

LONG guest_string(DWORD pointer, char *buffer, DWORD capacity) {
    for (DWORD index = 0; index < capacity; index++) {
        if (pointer > 0xffffffffUL - index || !guest_readable(pointer + index, 1)) return error(LINUX_EFAULT);
        buffer[index] = *(const char *)(pointer + index);
        if (!buffer[index]) return (LONG)index;
    }
    return error(LINUX_ENAMETOOLONG);
}

static int page_committed(DWORD address) {
    DWORD page = (address - GUEST_START) / PAGE_SIZE;
    return (state.committed[page / 32] >> (page % 32)) & 1;
}

static void mark_page(DWORD address, int committed) {
    DWORD page = (address - GUEST_START) / PAGE_SIZE, bit = 1UL << (page % 32);
    if (committed) state.committed[page / 32] |= bit;
    else state.committed[page / 32] &= ~bit;
}

static void count_pages(LONG change) {
    state.page_count += change;
    if (self) self->pages = state.page_count;
}

static void decommit_pages(DWORD start, DWORD end) {
    DWORD address = page_round_down(start);
    while (address < end) {
        if (!page_committed(address)) {
            address += PAGE_SIZE;
            continue;
        }
        DWORD run = address;
        while (run < end && page_committed(run)) {
            mark_page(run, 0);
            run += PAGE_SIZE;
        }
        VirtualFree((void *)address, run - address, MEM_DECOMMIT);
        count_pages(-(LONG)((run - address) / PAGE_SIZE));
        address = run;
    }
}

int commit_pages(DWORD start, DWORD length) {
    DWORD first = page_round_down(start), end = page_round_up(start + length), count = (end - first) / PAGE_SIZE;
    if (first < GUEST_START || end > GUEST_LIMIT || end <= first) return 0;
    if (state.page_count + count > MAX_PAGES) return 0;
    for (DWORD address = first; address < end; address += PAGE_SIZE)
        if (page_committed(address)) return 0;
    if (VirtualAlloc((void *)first, end - first, MEM_COMMIT, PAGE_READWRITE) != (void *)first) return 0;
    for (DWORD address = first; address < end; address += PAGE_SIZE) mark_page(address, 1);
    count_pages((LONG)count);
    return 1;
}

typedef BOOL (WINAPI *virtual_copy_function)(LPVOID, LPVOID, DWORD, DWORD);

int alias_pages(DWORD start, DWORD length, const void *source) {
    DWORD first = page_round_down(start), end = page_round_up(start + length), count = (end - first) / PAGE_SIZE;
    if (first != start || first < GUEST_START || end > GUEST_LIMIT || end <= first) return 0;
    if (((DWORD)source & (PAGE_SIZE - 1)) || state.page_count + count > MAX_PAGES) return 0;
    for (DWORD address = first; address < end; address += PAGE_SIZE)
        if (page_committed(address)) return 0;
    virtual_copy_function copy = (virtual_copy_function)jolt_optional(&jolt_virtual_copy);
    if (!copy || !copy((void *)first, (void *)source, end - first, PAGE_EXECUTE_READ)) return 0;
    for (DWORD address = first; address < end; address += PAGE_SIZE) mark_page(address, 1);
    count_pages((LONG)count);
    return 1;
}

int add_range(DWORD start, DWORD length, DWORD flags) {
    if (!length || state.range_count == MAX_RANGES) return 0;
    if (start < GUEST_START || start >= GUEST_LIMIT || length > GUEST_LIMIT - start) return 0;
    for (DWORD index = 0; index < state.range_count; index++) {
        const struct guest_range *range = &state.ranges[index];
        if (start < range->end && start + length > range->start) return 0;
    }
    state.ranges[state.range_count++] = (struct guest_range){ start, start + length, flags };
    return 1;
}

static void remove_range(DWORD index) {
    DWORD last = --state.range_count;
    if (index == last) return;
    state.ranges[index] = state.ranges[last];
    if (state.heap_range == (int)last) state.heap_range = (int)index;
}

static DWORD protection_for(DWORD flags) {
    if (flags & RANGE_EXECUTE) return PAGE_EXECUTE_READ;
    if (flags & RANGE_WRITE) return PAGE_READWRITE;
    return PAGE_READONLY;
}

int protect_range(const struct guest_range *range) {
    DWORD first = page_round_down(range->start), size = page_round_up(range->end) - first, previous = 0;
    if (!FlushInstructionCache(GetCurrentProcess(), (void *)first, size)) return 0;
    return VirtualProtect((void *)first, size, protection_for(range->flags), &previous) != 0;
}

void release_memory(void) {
    decommit_pages(GUEST_START, GUEST_LIMIT);
    state.range_count = 0;
    state.heap_start = state.heap_break = state.heap_end = 0;
    state.heap_range = -1;
}

static int grow_heap(DWORD end) {
    if (!commit_pages(state.heap_end, end - state.heap_end)) return 0;
    if (state.heap_range >= 0) {
        state.ranges[state.heap_range].end = end;
    } else if (add_range(state.heap_start, end - state.heap_start, RANGE_READ | RANGE_WRITE)) {
        state.heap_range = (int)state.range_count - 1;
    } else {
        decommit_pages(state.heap_end, end);
        return 0;
    }
    state.heap_end = end;
    return 1;
}

static void shrink_heap(DWORD end) {
    decommit_pages(end, state.heap_end);
    state.heap_end = end;
    if (state.heap_range < 0) return;
    if (end == state.heap_start) {
        remove_range((DWORD)state.heap_range);
        state.heap_range = -1;
    } else {
        state.ranges[state.heap_range].end = end;
    }
}

LONG sys_brk(const DWORD *arguments) {
    DWORD address = arguments[0];
    if (!address || address < state.heap_start || address > MMAP_START) return (LONG)state.heap_break;
    DWORD end = page_round_up(address);
    if (end > state.heap_end && !grow_heap(end)) return (LONG)state.heap_break;
    if (end < state.heap_end) shrink_heap(end);
    state.heap_break = address;
    return (LONG)address;
}

static DWORD find_mapping_space(DWORD size) {
    DWORD candidate = MMAP_START;
    for (DWORD index = 0; index < state.range_count; index++) {
        const struct guest_range *range = &state.ranges[index];
        if (candidate >= range->end || candidate + size <= range->start) continue;
        candidate = page_round_up(range->end);
        if (candidate > GATE_PAGE || size > GATE_PAGE - candidate) return 0;
        index = (DWORD)-1;
    }
    return candidate;
}

static int is_mapping(const struct guest_range *range) {
    return range->start >= MMAP_START && range->start < GATE_PAGE && range->flags == (RANGE_READ | RANGE_WRITE);
}

static int add_mapping(DWORD start, DWORD end) {
    int before = -1, after = -1;
    for (DWORD index = 0; index < state.range_count; index++) {
        const struct guest_range *range = &state.ranges[index];
        if (!is_mapping(range)) continue;
        if (range->end == start) before = (int)index;
        if (range->start == end) after = (int)index;
    }
    if (before >= 0 && after >= 0) {
        state.ranges[before].end = state.ranges[after].end;
        remove_range((DWORD)after);
        return 1;
    }
    if (before >= 0) {
        state.ranges[before].end = end;
        return 1;
    }
    if (after >= 0) {
        state.ranges[after].start = start;
        return 1;
    }
    return add_range(start, end - start, RANGE_READ | RANGE_WRITE);
}

LONG sys_mmap2(const DWORD *arguments) {
    DWORD length = arguments[1], protection = arguments[2], flags = arguments[3], fd = arguments[4];
    if ((flags & LINUX_MAP_FIXED) || !(flags & LINUX_MAP_ANONYMOUS) || fd != 0xffffffffUL) return error(LINUX_ENODEV);
    if (protection & ~(DWORD)(LINUX_PROT_READ | LINUX_PROT_WRITE)) return error(LINUX_ENODEV);
    DWORD size = page_round_up(length);
    if (!size || size > GATE_PAGE - MMAP_START) return error(LINUX_ENOMEM);
    DWORD address = find_mapping_space(size);
    if (!address || !commit_pages(address, size)) return error(LINUX_ENOMEM);
    if (!add_mapping(address, address + size)) {
        decommit_pages(address, address + size);
        return error(LINUX_ENOMEM);
    }
    return (LONG)address;
}

static int unmap_from_range(DWORD index, DWORD start, DWORD end) {
    struct guest_range *range = &state.ranges[index];
    DWORD cut_start = start > range->start ? start : range->start;
    DWORD cut_end = end < range->end ? end : range->end;
    decommit_pages(cut_start, cut_end);
    if (cut_start == range->start && cut_end == range->end) {
        remove_range(index);
        return 1;
    }
    if (cut_start == range->start) {
        range->start = cut_end;
    } else if (cut_end == range->end) {
        range->end = cut_start;
    } else {
        struct guest_range tail = { cut_end, range->end, range->flags };
        range->end = cut_start;
        if (state.range_count < MAX_RANGES) state.ranges[state.range_count++] = tail;
    }
    return 0;
}

LONG sys_munmap(const DWORD *arguments) {
    DWORD address = arguments[0], size = page_round_up(arguments[1]);
    if ((address & (PAGE_SIZE - 1)) || !size) return error(LINUX_EINVAL);
    if (address < MMAP_START || address >= GATE_PAGE || size > GATE_PAGE - address) return error(LINUX_EINVAL);
    DWORD end = address + size;
    for (DWORD index = 0; index < state.range_count; index++) {
        const struct guest_range *range = &state.ranges[index];
        int mapping = range->start >= MMAP_START && range->start < GATE_PAGE;
        if (!mapping || end <= range->start || address >= range->end) continue;
        if (unmap_from_range(index, address, end)) index = (DWORD)-1;
    }
    return 0;
}

LONG sys_mprotect(const DWORD *arguments) {
    return guest_readable(arguments[0], arguments[1]) ? 0 : error(LINUX_ENOMEM);
}

LONG sys_mremap(const DWORD *arguments) {
    (void)arguments;
    return error(LINUX_ENOMEM);
}
