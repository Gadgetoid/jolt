#pragma once
#include <windows.h>

struct guest_image {
    const BYTE *data;
    DWORD size;
    int shared;
    BYTE *allocation;
    HANDLE mapping;
    BYTE *view;
    DWORD key;
    DWORD mapping_size;
};

void image_use_shared(struct guest_image *image);
LONG image_load(const char *absolute, struct guest_image *image, int allow_unmarked);
void image_release(struct guest_image *image);
void image_adopt(struct guest_image *image);
int image_attach(void);
const BYTE *image_shared_data(void);
int image_is_elf(const char *header, LONG length);
