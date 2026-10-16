#include <windows.h>
#include <stdio.h>
#include <tlhelp32.h>
#include "memorymap.h"
#include "optional.h"
#include "text.h"
#include "worker.h"

#define MAP_FILE L"memory.txt"
#define SLOT_START 0x00010000UL
#define SLOT_END 0x02000000UL
#define SLOT_MASK 0x01ffffffUL
#define MODULE_LIMIT 96
#define LINE_CAPACITY 160
#define NAME_CAPACITY 40

typedef HANDLE (WINAPI *create_snapshot_function)(DWORD, DWORD);
typedef BOOL (WINAPI *module_walk_function)(HANDLE, MODULEENTRY32 *);
typedef BOOL (WINAPI *close_snapshot_function)(HANDLE);

struct module {
    DWORD base, size;
    char name[NAME_CAPACITY];
};

static struct module modules[MODULE_LIMIT];
static int module_count;

static void collect_modules(void) {
    create_snapshot_function create = (create_snapshot_function)jolt_optional(&jolt_create_snapshot);
    module_walk_function first = (module_walk_function)jolt_optional(&jolt_module_first);
    module_walk_function next = (module_walk_function)jolt_optional(&jolt_module_next);
    close_snapshot_function close = (close_snapshot_function)jolt_optional(&jolt_close_snapshot);
    module_count = 0;
    if (!create || !first || !next || !close) return;
    HANDLE snapshot = create(TH32CS_SNAPMODULE, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32 entry;
    memset(&entry, 0, sizeof entry);
    entry.dwSize = sizeof entry;
    for (BOOL more = first(snapshot, &entry); more && module_count < MODULE_LIMIT; more = next(snapshot, &entry)) {
        struct module *module = &modules[module_count++];
        module->base = (DWORD)entry.modBaseAddr & SLOT_MASK;
        module->size = entry.modBaseSize;
        jolt_ascii_from_wide(entry.szModule, module->name, NAME_CAPACITY);
        entry.dwSize = sizeof entry;
    }
    close(snapshot);
}

static const char *module_at(DWORD address) {
    for (int index = 0; index < module_count; index++)
        if (address >= modules[index].base && address < modules[index].base + modules[index].size) return modules[index].name;
    return "";
}

static void write_line(HANDLE file, const char *format, ...) {
    char line[LINE_CAPACITY];
    va_list list;
    va_start(list, format);
    int length = vsnprintf(line, sizeof line, format, list);
    va_end(list);
    if (length <= 0) return;
    if (length >= (int)sizeof line) length = sizeof line - 1;
    DWORD written;
    WriteFile(file, line, (DWORD)length, &written, NULL);
}

void memory_map_write(const WCHAR *folder) {
    WCHAR path[MAX_PATH];
    wsprintfW(path, L"%s%s", folder, MAP_FILE);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    collect_modules();
    write_line(file, "JoLT worker memory, guest range %08lx-%08lx\r\n\r\nmodules:\r\n", GUEST_RESERVE_BASE,
        GUEST_RESERVE_LIMIT);
    for (int index = 0; index < module_count; index++)
        write_line(file, "  %08lx %8lu %s\r\n", modules[index].base, modules[index].size, modules[index].name);
    write_line(file, "\r\nregions (base, size, allocation base, state, type, module):\r\n");
    DWORD address = SLOT_START;
    while (address < SLOT_END) {
        MEMORY_BASIC_INFORMATION area;
        if (!VirtualQuery((void *)address, &area, sizeof area) || !area.RegionSize) break;
        DWORD base = (DWORD)area.BaseAddress & SLOT_MASK, allocation = (DWORD)area.AllocationBase & SLOT_MASK;
        if (area.State != MEM_FREE)
            write_line(file, "  %08lx %8lu %08lx %5lx %7lx %s\r\n", base, area.RegionSize, allocation, area.State, area.Type,
                module_at(allocation));
        address = base + area.RegionSize;
    }
    CloseHandle(file);
}
