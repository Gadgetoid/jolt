#include <windows.h>
#include <string.h>
#include "memory.h"
#include "syscalls.h"
#include "optional.h"

#define HEAP_HEADROOM 0x00040000UL

static DWORD reserved_limit;

DWORD page_round_up(DWORD value) {
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static DWORD page_round_down(DWORD value) {
    return value & ~(PAGE_SIZE - 1);
}

static int range_allows(DWORD pointer, DWORD length, DWORD permission) {
    if (!length) return 1;
    if (pointer < GUEST_START || pointer >= state.guest_limit || length > state.guest_limit - pointer) return 0;
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
    if (first < GUEST_START || end > state.guest_limit || end <= first) return 0;
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
    if (first != start || first < GUEST_START || end > state.guest_limit || end <= first) return 0;
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
    if (start < GUEST_START || start >= state.guest_limit || length > state.guest_limit - start) return 0;
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
    decommit_pages(GUEST_START, state.guest_limit);
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

static DWORD lowest_allocation_above(DWORD address) {
    while (address < GUEST_CEILING) {
        MEMORY_BASIC_INFORMATION area;
        if (!VirtualQuery((void *)address, &area, sizeof area) || !area.RegionSize || area.State != MEM_FREE) return address;
        address = ((DWORD)area.BaseAddress & SLOT_MASK) + area.RegionSize;
    }
    return GUEST_CEILING;
}

DWORD guest_reserve_ceiling(void) {
    DWORD free_end = lowest_allocation_above(GUEST_RESERVE_LIMIT);
    if (free_end < GUEST_RESERVE_LIMIT + DLL_MARGIN) return GUEST_RESERVE_LIMIT;
    DWORD limit = (free_end - DLL_MARGIN) & ~(RESERVE_GRANULARITY - 1);
    return limit > GUEST_RESERVE_LIMIT ? limit : GUEST_RESERVE_LIMIT;
}

int guest_reserve(DWORD limit) {
    if (VirtualAlloc((void *)GUEST_RESERVE_BASE, limit - GUEST_RESERVE_BASE, MEM_RESERVE, PAGE_NOACCESS) !=
        (void *)GUEST_RESERVE_BASE) return 0;
    reserved_limit = limit;
    state.guest_limit = limit;
    return 1;
}

int guest_reserve_at_least(DWORD limit) {
    if (limit > reserved_limit) {
        DWORD previous = reserved_limit;
        VirtualFree((void *)GUEST_RESERVE_BASE, 0, MEM_RELEASE);
        if (!guest_reserve(limit)) {
            guest_reserve(previous);
            return 0;
        }
    }
    state.guest_limit = reserved_limit;
    return 1;
}

static int space_free(DWORD start, DWORD end) {
    for (DWORD index = 0; index < state.range_count; index++) {
        const struct guest_range *range = &state.ranges[index];
        if (start < range->end && end > range->start) return 0;
    }
    return 1;
}

static DWORD find_space_upward(DWORD low, DWORD high, DWORD size) {
    DWORD candidate = low;
    for (DWORD index = 0; index < state.range_count; index++) {
        const struct guest_range *range = &state.ranges[index];
        if (candidate >= range->end || candidate + size <= range->start) continue;
        candidate = page_round_up(range->end);
        index = (DWORD)-1;
    }
    return candidate <= high && size <= high - candidate ? candidate : 0;
}

static DWORD find_space_downward(DWORD low, DWORD high, DWORD size) {
    if (high <= low || size > high - low) return 0;
    DWORD candidate = high - size;
    for (DWORD index = 0; index < state.range_count; index++) {
        const struct guest_range *range = &state.ranges[index];
        if (candidate >= range->end || candidate + size <= range->start) continue;
        DWORD below = page_round_down(range->start);
        if (below < low || size > below - low) return 0;
        candidate = below - size;
        index = (DWORD)-1;
    }
    return candidate;
}

static DWORD heap_gap_start(void) {
    DWORD heap_top = state.heap_end > state.heap_start ? state.heap_end : state.heap_start;
    if (!heap_top) return MMAP_START;
    heap_top = page_round_up(heap_top) + HEAP_HEADROOM;
    return heap_top < MMAP_START ? heap_top : MMAP_START;
}

static DWORD find_mapping_space(DWORD size) {
    DWORD address = find_space_upward(MMAP_START, GATE_PAGE, size);
    if (!address && state.guest_limit > STACK_LIMIT) address = find_space_upward(STACK_LIMIT, state.guest_limit, size);
    if (!address) address = find_space_downward(heap_gap_start(), MMAP_START, size);
    return address;
}

static int is_mapping(const struct guest_range *range) {
    return (range->flags & RANGE_MAPPING) != 0;
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
    return add_range(start, end - start, RANGE_READ | RANGE_WRITE | RANGE_MAPPING);
}

LONG sys_mmap2(const DWORD *arguments) {
    DWORD length = arguments[1], protection = arguments[2], flags = arguments[3], fd = arguments[4];
    if ((flags & LINUX_MAP_FIXED) || !(flags & LINUX_MAP_ANONYMOUS) || fd != 0xffffffffUL) return error(LINUX_ENODEV);
    if (protection & ~(DWORD)(LINUX_PROT_READ | LINUX_PROT_WRITE)) return error(LINUX_ENODEV);
    DWORD size = page_round_up(length);
    if (!size || size > state.guest_limit - GUEST_START) return error(LINUX_ENOMEM);
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
    int splits = cut_start > range->start && cut_end < range->end;
    if (splits && state.range_count == MAX_RANGES) return -1;
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
        state.ranges[state.range_count++] = tail;
    }
    return 0;
}

static LONG unmap(DWORD address, DWORD end) {
    for (DWORD index = 0; index < state.range_count; index++) {
        const struct guest_range *range = &state.ranges[index];
        if (!is_mapping(range) || end <= range->start || address >= range->end) continue;
        int result = unmap_from_range(index, address, end);
        if (result < 0) return error(LINUX_ENOMEM);
        if (result) index = (DWORD)-1;
    }
    return 0;
}

static int mappable(DWORD address, DWORD size) {
    return address >= GUEST_START && address < state.guest_limit && size <= state.guest_limit - address;
}

LONG sys_munmap(const DWORD *arguments) {
    DWORD address = arguments[0], size = page_round_up(arguments[1]);
    if ((address & (PAGE_SIZE - 1)) || !size || !mappable(address, size)) return error(LINUX_EINVAL);
    return unmap(address, address + size);
}

LONG sys_mprotect(const DWORD *arguments) {
    return guest_readable(arguments[0], arguments[1]) ? 0 : error(LINUX_ENOMEM);
}

static int mapped(DWORD start, DWORD end) {
    for (DWORD index = 0; index < state.range_count; index++) {
        const struct guest_range *range = &state.ranges[index];
        if (is_mapping(range) && range->start <= start && range->end >= end) return 1;
    }
    return 0;
}

LONG sys_mremap(const DWORD *arguments) {
    DWORD address = arguments[0], old_size = page_round_up(arguments[1]), new_size = page_round_up(arguments[2]);
    if ((address & (PAGE_SIZE - 1)) || !old_size || !new_size || !mappable(address, old_size)) return error(LINUX_EINVAL);
    DWORD end = address + old_size;
    if (!mapped(address, end)) return error(LINUX_EFAULT);
    if (new_size == old_size) return (LONG)address;
    if (new_size < old_size) {
        LONG result = unmap(address + new_size, end);
        return result ? result : (LONG)address;
    }
    if (!mappable(address, new_size)) return error(LINUX_ENOMEM);
    DWORD new_end = address + new_size;
    if (!space_free(end, new_end) || !commit_pages(end, new_end - end)) return error(LINUX_ENOMEM);
    if (!add_mapping(end, new_end)) {
        decommit_pages(end, new_end);
        return error(LINUX_ENOMEM);
    }
    return (LONG)address;
}
