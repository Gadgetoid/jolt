#include <windows.h>
#include <string.h>
#include <stddef.h>
#include "worker.h"
#include "memory.h"
#include "process.h"
#include "random.h"
#include "elf.h"

#define AUX_PHDR 3
#define AUX_PHENT 4
#define AUX_PHNUM 5
#define AUX_PAGESZ 6
#define AUX_ENTRY 9
#define AUX_UID 11
#define AUX_EUID 12
#define AUX_GID 13
#define AUX_EGID 14
#define AUX_HWCAP 16
#define AUX_CLKTCK 17
#define AUX_SECURE 23
#define AUX_RANDOM 25
#define AUX_EXECFN 31
#define AUX_JOLT_GATE 0x70000001UL

#define ARM_LOAD_R12_FROM_PC 0xe59fc000UL
#define ARM_LOAD_PC_FROM_PC 0xe59ff000UL
#define ARM_POP_LR_PC 0xe8bdc000UL
#define GATE_DESCRIPTOR_WORD 16
#define GATE_DESCRIPTOR_MAGIC 0x4a4f4c54UL
#define GATE_DESCRIPTOR_VERSION 1

#define STACK_RESERVE 16384
#define RANDOM_BYTES 16
#define STRING_LIMIT 255

struct loaded_image {
    DWORD entry, headers, header_count, end;
};

_Static_assert(offsetof(struct jolt_context, exit_code) == 16, "Bridge context ABI");

static int valid_header(const struct elf_header *elf, DWORD size) {
    return !memcmp(elf->identity, "\177ELF", 4) && elf->identity[4] == ELF_CLASS_32 &&
        elf->identity[5] == ELF_DATA_LITTLE && elf->type == ELF_TYPE_EXECUTABLE &&
        elf->machine == ELF_MACHINE_ARM && elf->version == ELF_VERSION_CURRENT &&
        elf->header_size == sizeof *elf && elf->program_size == sizeof(struct elf_program) &&
        elf->program_count && elf->program_count <= ELF_PROGRAM_LIMIT && elf->program_offset <= size &&
        (DWORD)elf->program_count * sizeof(struct elf_program) <= size - elf->program_offset &&
        (elf->flags & ELF_ARM_EABI_MASK) == ELF_ARM_EABI_5 && !(elf->entry & 3);
}

static int valid_segment(const struct elf_program *program, DWORD size) {
    int writable_and_executable = (program->flags & (RANGE_EXECUTE | RANGE_WRITE)) == (RANGE_EXECUTE | RANGE_WRITE);
    return program->address >= GUEST_START && program->address < IMAGE_LIMIT &&
        program->memory_size <= IMAGE_LIMIT - program->address &&
        !(program->flags & ~(DWORD)(RANGE_EXECUTE | RANGE_WRITE | RANGE_READ)) && !writable_and_executable &&
        program->alignment == PAGE_SIZE && (program->address & (PAGE_SIZE - 1)) == (program->offset & (PAGE_SIZE - 1)) &&
        program->file_size <= program->memory_size && program->offset <= size &&
        program->file_size <= size - program->offset;
}

static DWORD alias_shared_text(const struct elf_program *program, const BYTE *image, int shared) {
    if (!shared || !(program->flags & RANGE_EXECUTE) || (program->address & (PAGE_SIZE - 1))) return 0;
    DWORD length = program->file_size & ~(PAGE_SIZE - 1);
    if (!length || !alias_pages(program->address, length, image + program->offset)) return 0;
    return length;
}

static int load_segment(const struct elf_program *program, const BYTE *image, int shared) {
    if (!add_range(program->address, program->memory_size, program->flags)) return 0;
    DWORD aliased = alias_shared_text(program, image, shared);
    if (aliased < program->memory_size && !commit_pages(program->address + aliased, program->memory_size - aliased)) return 0;
    memcpy((void *)(program->address + aliased), image + program->offset + aliased, program->file_size - aliased);
    memset((void *)(program->address + program->file_size), 0, program->memory_size - program->file_size);
    if (!protect_range(&state.ranges[state.range_count - 1])) return 0;
    if (program->flags & RANGE_EXECUTE) {
        state.text_start = program->address & ~(PAGE_SIZE - 1);
        state.text_end = (program->address + program->file_size) & ~(PAGE_SIZE - 1);
        state.text_offset = program->offset - (program->address & (PAGE_SIZE - 1));
    }
    return 1;
}

static int load_elf(const struct guest_image *source, struct loaded_image *loaded) {
    const BYTE *image = source->data;
    DWORD size = source->size;
    const struct elf_header *elf = (const struct elf_header *)image;
    if (size < sizeof *elf || !valid_header(elf, size)) return 0;
    const struct elf_program *programs = (const struct elf_program *)(image + elf->program_offset);
    int entry_executable = 0;
    memset(loaded, 0, sizeof *loaded);
    loaded->end = GUEST_START;
    for (unsigned int index = 0; index < elf->program_count; index++) {
        const struct elf_program *program = &programs[index];
        if (program->type == ELF_PHDR) loaded->headers = program->address;
        if (program->type != ELF_LOAD) continue;
        if (!valid_segment(program, size) || !load_segment(program, image, source->shared)) return 0;
        DWORD end = page_round_up(program->address + program->memory_size);
        if (end > loaded->end) loaded->end = end;
        int contains_entry = elf->entry >= program->address && elf->entry < program->address + program->memory_size;
        if ((program->flags & RANGE_EXECUTE) && contains_entry) entry_executable = 1;
    }
    if (!entry_executable || !loaded->headers) return 0;
    if (!guest_readable(loaded->headers, elf->program_count * sizeof(struct elf_program))) return 0;
    loaded->entry = elf->entry;
    loaded->header_count = elf->program_count;
    return 1;
}

static int setup_gate(void) {
    if (!add_range(GATE_PAGE, PAGE_SIZE, RANGE_READ | RANGE_EXECUTE) || !commit_pages(GATE_PAGE, PAGE_SIZE)) return 0;
    DWORD *words = (DWORD *)GATE_PAGE;
    words[0] = ARM_LOAD_R12_FROM_PC;
    words[1] = ARM_LOAD_PC_FROM_PC;
    words[2] = (DWORD)&state.context;
    words[3] = (DWORD)jolt_gate;
    words[4] = ARM_POP_LR_PC;
    words[GATE_DESCRIPTOR_WORD] = GATE_DESCRIPTOR_MAGIC;
    words[GATE_DESCRIPTOR_WORD + 1] = GATE_DESCRIPTOR_VERSION;
    words[GATE_DESCRIPTOR_WORD + 2] = GATE_PAGE;
    return protect_range(&state.ranges[state.range_count - 1]);
}

static DWORD push_string(DWORD *cursor, DWORD limit, const char *text) {
    DWORD length = (DWORD)strlen(text) + 1;
    if (*cursor - limit < length) return 0;
    *cursor -= length;
    memcpy((void *)*cursor, text, length);
    return *cursor;
}

static DWORD push_strings(DWORD *cursor, DWORD limit, char **strings, DWORD *pointers) {
    DWORD count = 0;
    for (; strings[count]; count++) {
        if (count == STRING_LIMIT) return (DWORD)-1;
        pointers[count] = push_string(cursor, limit, strings[count]);
        if (!pointers[count]) return (DWORD)-1;
    }
    return count;
}

static DWORD build_stack(const char *path, char **arguments, char **environment, const struct loaded_image *loaded) {
    if (!add_range(STACK_BASE, STACK_SIZE, RANGE_READ | RANGE_WRITE) || !commit_pages(STACK_BASE, STACK_SIZE)) return 0;
    DWORD limit = STACK_BASE + STACK_RESERVE;
    DWORD cursor = STACK_LIMIT - RANDOM_BYTES;
    DWORD random = cursor;
    random_fill_fast((BYTE *)random, RANDOM_BYTES);
    DWORD execfn = push_string(&cursor, limit, path);
    DWORD argument_pointers[STRING_LIMIT], environment_pointers[STRING_LIMIT];
    DWORD argument_count = push_strings(&cursor, limit, arguments, argument_pointers);
    DWORD environment_count = push_strings(&cursor, limit, environment, environment_pointers);
    if (!execfn || argument_count == (DWORD)-1 || environment_count == (DWORD)-1) return 0;
    DWORD auxiliary[] = {
        AUX_PHDR, loaded->headers, AUX_PHENT, sizeof(struct elf_program), AUX_PHNUM, loaded->header_count,
        AUX_PAGESZ, PAGE_SIZE, AUX_ENTRY, loaded->entry, AUX_UID, 0, AUX_EUID, 0, AUX_GID, 0, AUX_EGID, 0,
        AUX_HWCAP, 0, AUX_CLKTCK, 100, AUX_SECURE, 0, AUX_RANDOM, random, AUX_EXECFN, execfn,
        AUX_JOLT_GATE, GATE_PAGE + GATE_DESCRIPTOR_WORD * sizeof(DWORD), 0, 0
    };
    DWORD words = 1 + argument_count + 1 + environment_count + 1 + sizeof auxiliary / sizeof auxiliary[0];
    DWORD stack = ((cursor & ~7UL) - words * sizeof(DWORD)) & ~7UL;
    if (stack < limit) return 0;
    DWORD *output = (DWORD *)stack;
    *output++ = argument_count;
    memcpy(output, argument_pointers, argument_count * sizeof(DWORD));
    output += argument_count;
    *output++ = 0;
    memcpy(output, environment_pointers, environment_count * sizeof(DWORD));
    output += environment_count;
    *output++ = 0;
    memcpy(output, auxiliary, sizeof auxiliary);
    return stack;
}

static void reset_signal_handlers(void) {
    for (int signal = 1; signal <= LINUX_SIGNAL_LIMIT; signal++) {
        if (state.actions[signal].handler == LINUX_SIG_DFL || state.actions[signal].handler == LINUX_SIG_IGN) continue;
        memset(&state.actions[signal], 0, sizeof state.actions[signal]);
    }
    state.saved_count = 0;
    state.suspended = 0;
    update_signal_masks();
}

static void record_command(const char *path, char **arguments) {
    DWORD used = 0;
    if (!arguments[0]) arguments = (char *[]){ (char *)path, NULL };
    for (int index = 0; arguments[index]; index++) {
        DWORD length = (DWORD)strlen(arguments[index]) + 1;
        if (used + length > JOLT_COMMAND_LINE) length = JOLT_COMMAND_LINE - used;
        if (!length) break;
        memcpy(self->command + used, arguments[index], length);
        used += length;
        self->command[used - 1] = 0;
    }
    self->command_length = used;
    self->start_tick = GetTickCount();
}

static void close_on_exec(const char *path, char **arguments) {
    jolt_lock();
    record_command(path, arguments);
    for (int fd = 0; fd < JOLT_FDS; fd++)
        if (self->cloexec[fd / 32] & (1UL << (fd % 32))) close_local(fd);
    const char *name = arguments[0] ? arguments[0] : path;
    const char *base = strrchr(name, '/');
    strncpy(self->name, base ? base + 1 : name, sizeof self->name - 1);
    self->name[sizeof self->name - 1] = 0;
    jolt_unlock();
}

LONG worker_exec(const char *path, char **arguments, char **environment, struct guest_image *image) {
    release_memory();
    reset_signal_handlers();
    close_on_exec(path, arguments);
    struct loaded_image loaded;
    image_adopt(image);
    if (!load_elf(image, &loaded) || !setup_gate()) return -1;
    DWORD stack = build_stack(path, arguments, environment, &loaded);
    if (!stack) return -1;
    state.heap_start = state.heap_break = state.heap_end = loaded.end;
    state.entry = loaded.entry;
    state.stack = stack;
    trace("exec %s entry %08lx stack %08lx\r\n", path, loaded.entry, stack);
    return 0;
}
