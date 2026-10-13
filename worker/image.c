#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "image.h"
#include "kernel.h"
#include "linux.h"
#include "files.h"
#include "elf.h"
#include "abi.h"
#include "worker.h"

#define IMAGE_FILE_LIMIT (IMAGE_LIMIT - GUEST_START)
#define NOTE_ALIGNMENT 4
#define REFUSAL_CAPACITY 384

static const char elf_magic[4] = { 0x7f, 'E', 'L', 'F' };

int image_is_elf(const char *header, LONG length) {
    return length >= (LONG)sizeof elf_magic && !memcmp(header, elf_magic, sizeof elf_magic);
}

void image_use_shared(struct guest_image *image) {
    memset(image, 0, sizeof *image);
    image->data = jolt.image;
    image->size = jolt.kernel->image_size;
    image->shared = 1;
}

static DWORD align_note(DWORD value) {
    return (value + NOTE_ALIGNMENT - 1) & ~(DWORD)(NOTE_ALIGNMENT - 1);
}

static int note_marks_jolt(const BYTE *notes, DWORD size) {
    DWORD offset = 0;
    while (offset + sizeof(struct elf_note) <= size) {
        const struct elf_note *note = (const struct elf_note *)(notes + offset);
        DWORD name = offset + sizeof *note;
        DWORD description = name + align_note(note->name_size);
        DWORD next = description + align_note(note->description_size);
        if (next > size || next <= offset) return 0;
        int named = note->name_size == sizeof JOLT_NOTE_NAME && !memcmp(notes + name, JOLT_NOTE_NAME, sizeof JOLT_NOTE_NAME);
        if (named && note->type == JOLT_NOTE_ABI && note->description_size == sizeof(DWORD)) {
            DWORD version;
            memcpy(&version, notes + description, sizeof version);
            return version == JOLT_ABI_VERSION;
        }
        offset = next;
    }
    return 0;
}

static int marked_for_jolt(const BYTE *data, DWORD size) {
    if (size < sizeof(struct elf_header)) return 0;
    const struct elf_header *elf = (const struct elf_header *)data;
    if (elf->program_size != sizeof(struct elf_program) || elf->program_count > ELF_PROGRAM_LIMIT) return 0;
    if (elf->program_offset > size || elf->program_count * sizeof(struct elf_program) > size - elf->program_offset) return 0;
    const struct elf_program *programs = (const struct elf_program *)(data + elf->program_offset);
    for (WORD index = 0; index < elf->program_count; index++) {
        const struct elf_program *program = &programs[index];
        if (program->type != ELF_NOTE || program->offset > size || program->file_size > size - program->offset) continue;
        if (note_marks_jolt(data + program->offset, program->file_size)) return 1;
    }
    return 0;
}

static void report_unmarked(const char *absolute) {
    char text[REFUSAL_CAPACITY];
    int length = snprintf(text, sizeof text,
        "jolt: %s was not built for JoLT (set " JOLT_ALLOW_UNMARKED " to try it anyway)\n", absolute);
    if (length > 0) jolt_tty_write(text, (DWORD)(length < (int)sizeof text ? length : (int)sizeof text - 1), 1);
}

LONG image_load(const char *absolute, struct guest_image *image, int allow_unmarked) {
    memset(image, 0, sizeof *image);
    LONG result = jolt_file_load(absolute, &image->allocation, &image->size, IMAGE_FILE_LIMIT);
    if (result) return result;
    if (!allow_unmarked && !marked_for_jolt(image->allocation, image->size)) {
        report_unmarked(absolute);
        image_release(image);
        return -(LONG)LINUX_EACCES;
    }
    image->data = image->allocation;
    return 0;
}

void image_release(struct guest_image *image) {
    if (image->allocation) LocalFree(image->allocation);
    memset(image, 0, sizeof *image);
}
