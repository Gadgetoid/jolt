#pragma once
#include <windows.h>

#define ELF_CLASS_32 1
#define ELF_DATA_LITTLE 1
#define ELF_TYPE_EXECUTABLE 2
#define ELF_MACHINE_ARM 40
#define ELF_VERSION_CURRENT 1
#define ELF_ARM_EABI_5 0x05000000UL
#define ELF_ARM_EABI_MASK 0xff000000UL
#define ELF_PROGRAM_LIMIT 8
#define ELF_LOAD 1
#define ELF_NOTE 4
#define ELF_PHDR 6

struct elf_header {
    BYTE identity[16];
    WORD type, machine;
    DWORD version, entry, program_offset, section_offset, flags;
    WORD header_size, program_size, program_count, section_size, section_count, section_names;
};

struct elf_program {
    DWORD type, offset, address, physical, file_size, memory_size, flags, alignment;
};

struct elf_note {
    DWORD name_size, description_size, type;
};

_Static_assert(sizeof(struct elf_header) == 52, "ELF header size");
_Static_assert(sizeof(struct elf_program) == 32, "ELF program size");
_Static_assert(sizeof(struct elf_note) == 12, "ELF note size");
