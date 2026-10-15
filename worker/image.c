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
#define CACHE_MAGIC 0x4a6f4c54UL
#define CACHE_NAME_CAPACITY 48
#define FNV_OFFSET 2166136261UL
#define FNV_PRIME 16777619UL

struct cache_header {
    DWORD magic;
    DWORD size;
    FILETIME written;
    WCHAR path[MAX_PATH];
};

struct cached_image {
    HANDLE mapping;
    BYTE *view;
};

static struct cached_image current;

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
    if (length > 0) jolt_tty_write(self->ctty == JOLT_NO_TTY ? JOLT_CONSOLE : self->ctty, text, (DWORD)(length < (int)sizeof text ? length : (int)sizeof text - 1), 1);
}

static DWORD hash_bytes(DWORD hash, const void *data, DWORD length) {
    const BYTE *bytes = data;
    for (DWORD index = 0; index < length; index++) hash = (hash ^ bytes[index]) * FNV_PRIME;
    return hash;
}

static DWORD cache_key(const WCHAR *path, DWORD size, const FILETIME *written) {
    DWORD hash = hash_bytes(FNV_OFFSET, path, (DWORD)(wcslen(path) * sizeof(WCHAR)));
    hash = hash_bytes(hash, &size, sizeof size);
    hash = hash_bytes(hash, written, sizeof *written);
    return hash ? hash : 1;
}

static DWORD header_offset(DWORD size) {
    return (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static struct cache_header *cache_header(BYTE *view, DWORD size) {
    return (struct cache_header *)(view + header_offset(size));
}

static int open_mapping(DWORD key, DWORD mapping_size, struct cached_image *cached, int *existed) {
    WCHAR name[CACHE_NAME_CAPACITY];
    jolt_image_name(name, key);
    cached->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, mapping_size, name);
    if (!cached->mapping) return 0;
    *existed = GetLastError() == ERROR_ALREADY_EXISTS;
    cached->view = MapViewOfFile(cached->mapping, FILE_MAP_WRITE, 0, 0, mapping_size);
    if (cached->view) return 1;
    CloseHandle(cached->mapping);
    cached->mapping = NULL;
    return 0;
}

static void close_mapping(struct cached_image *cached) {
    if (cached->view) UnmapViewOfFile(cached->view);
    if (cached->mapping) CloseHandle(cached->mapping);
    cached->view = NULL;
    cached->mapping = NULL;
}

static int header_matches(const struct cache_header *header, const WCHAR *path, DWORD size, const FILETIME *written) {
    return header->magic == CACHE_MAGIC && header->size == size && !memcmp(&header->written, written, sizeof *written) &&
        !wcscmp(header->path, path);
}

static int fill_cache(HANDLE file, BYTE *view, const WCHAR *path, DWORD size, const FILETIME *written) {
    struct cache_header *header = cache_header(view, size);
    DWORD count = 0;
    if (!ReadFile(file, view, size, &count, NULL) || count != size) return 0;
    header->size = size;
    header->written = *written;
    wcsncpy(header->path, path, MAX_PATH - 1);
    header->magic = CACHE_MAGIC;
    return 1;
}

static int load_cached(HANDLE file, const WCHAR *path, struct guest_image *image) {
    DWORD size = GetFileSize(file, NULL);
    FILETIME written;
    if (size == INVALID_FILE_SIZE || !size || size > IMAGE_FILE_LIMIT || wcslen(path) >= MAX_PATH) return 0;
    if (!GetFileTime(file, NULL, NULL, &written)) return 0;
    DWORD key = cache_key(path, size, &written), mapping_size = header_offset(size) + sizeof(struct cache_header);
    struct cached_image cached;
    int existed = 0;
    if (!open_mapping(key, mapping_size, &cached, &existed)) return 0;
    const struct cache_header *header = cache_header(cached.view, size);
    int ready = existed ? header_matches(header, path, size, &written) : fill_cache(file, cached.view, path, size, &written);
    if (!ready) {
        close_mapping(&cached);
        return 0;
    }
    image->mapping = cached.mapping;
    image->view = cached.view;
    image->data = cached.view;
    image->size = size;
    image->key = key;
    image->mapping_size = mapping_size;
    image->shared = 1;
    jolt_image_touch(key, mapping_size);
    HWND window = (HWND)jolt.kernel->window;
    if (!existed && window) SendMessageW(window, JOLT_WARM_MESSAGE, key, mapping_size);
    return 1;
}

LONG image_load(const char *absolute, struct guest_image *image, int allow_unmarked) {
    memset(image, 0, sizeof *image);
    HANDLE file;
    WCHAR path[MAX_PATH];
    LONG result = jolt_file_open_host(absolute, &file, path);
    if (result) return result;
    int cached = load_cached(file, path, image);
    CloseHandle(file);
    if (!cached) {
        result = jolt_file_load(absolute, &image->allocation, &image->size, IMAGE_FILE_LIMIT);
        if (result) return result;
        image->data = image->allocation;
    }
    if (!allow_unmarked && !marked_for_jolt(image->data, image->size)) {
        report_unmarked(absolute);
        image_release(image);
        return -(LONG)LINUX_EACCES;
    }
    return 0;
}

void image_release(struct guest_image *image) {
    if (image->allocation) LocalFree(image->allocation);
    struct cached_image cached = { image->mapping, image->view };
    close_mapping(&cached);
    memset(image, 0, sizeof *image);
}

void image_adopt(struct guest_image *image) {
    close_mapping(&current);
    current.mapping = image->mapping;
    current.view = image->view;
    image->mapping = NULL;
    image->view = NULL;
    state.image_shared = image->shared;
    state.image_key = image->key;
    state.image_mapping_size = image->mapping_size;
}

int image_attach(void) {
    int existed = 0;
    if (!state.image_key) return 1;
    if (open_mapping(state.image_key, state.image_mapping_size, &current, &existed)) {
        const struct cache_header *header = (const struct cache_header *)(current.view + state.image_mapping_size - sizeof(struct cache_header));
        if (existed && header->magic == CACHE_MAGIC) return 1;
        close_mapping(&current);
    }
    state.image_shared = 0;
    state.image_key = 0;
    return 0;
}

const BYTE *image_shared_data(void) {
    if (!state.image_key) return jolt.image;
    return current.view;
}
