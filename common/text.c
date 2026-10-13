#include "text.h"

#define REPLACEMENT_CHARACTER 0xfffd

static DWORD put_utf8(DWORD code, char *text, DWORD used, DWORD capacity) {
    BYTE bytes[4];
    DWORD count;
    if (code < 0x80) {
        bytes[0] = (BYTE)code;
        count = 1;
    } else if (code < 0x800) {
        bytes[0] = (BYTE)(0xc0 | (code >> 6));
        bytes[1] = (BYTE)(0x80 | (code & 0x3f));
        count = 2;
    } else if (code < 0x10000) {
        bytes[0] = (BYTE)(0xe0 | (code >> 12));
        bytes[1] = (BYTE)(0x80 | ((code >> 6) & 0x3f));
        bytes[2] = (BYTE)(0x80 | (code & 0x3f));
        count = 3;
    } else {
        bytes[0] = (BYTE)(0xf0 | (code >> 18));
        bytes[1] = (BYTE)(0x80 | ((code >> 12) & 0x3f));
        bytes[2] = (BYTE)(0x80 | ((code >> 6) & 0x3f));
        bytes[3] = (BYTE)(0x80 | (code & 0x3f));
        count = 4;
    }
    if (used + count > capacity) return 0;
    for (DWORD index = 0; index < count; index++) text[used + index] = (char)bytes[index];
    return count;
}

DWORD jolt_utf8_from_wide(const WCHAR *wide, char *text, DWORD capacity) {
    DWORD used = 0;
    for (; *wide; wide++) {
        DWORD code = *wide;
        if (code == L'\r' && wide[1] == L'\n') continue;
        if (code >= 0xd800 && code < 0xdc00 && wide[1] >= 0xdc00 && wide[1] < 0xe000) {
            code = 0x10000 + ((code - 0xd800) << 10) + (wide[1] - 0xdc00);
            wide++;
        } else if (code >= 0xd800 && code < 0xe000) {
            code = REPLACEMENT_CHARACTER;
        }
        DWORD count = put_utf8(code, text, used, capacity);
        if (!count) break;
        used += count;
    }
    return used;
}

static DWORD sequence_length(BYTE lead) {
    if (lead >= 0xf0 && lead < 0xf8) return 4;
    if (lead >= 0xe0) return lead < 0xf0 ? 3 : 0;
    if (lead >= 0xc0) return 2;
    return 0;
}

DWORD jolt_wide_from_utf8(const char *text, DWORD length, WCHAR *wide, DWORD capacity) {
    DWORD used = 0;
    for (DWORD index = 0; index < length;) {
        BYTE lead = (BYTE)text[index];
        DWORD code = REPLACEMENT_CHARACTER, count = 1;
        if (lead < 0x80) {
            code = lead;
        } else {
            DWORD expected = sequence_length(lead);
            if (expected && index + expected <= length) {
                code = lead & (0xff >> (expected + 1));
                count = expected;
                for (DWORD offset = 1; offset < expected; offset++) {
                    BYTE next = (BYTE)text[index + offset];
                    if ((next & 0xc0) != 0x80) {
                        code = REPLACEMENT_CHARACTER;
                        count = offset;
                        break;
                    }
                    code = (code << 6) | (next & 0x3f);
                }
            }
        }
        index += count;
        if (code == '\n') {
            if (used + 2 >= capacity) break;
            wide[used++] = L'\r';
            wide[used++] = L'\n';
        } else if (code >= 0x10000 && code < 0x110000) {
            if (used + 2 >= capacity) break;
            code -= 0x10000;
            wide[used++] = (WCHAR)(0xd800 + (code >> 10));
            wide[used++] = (WCHAR)(0xdc00 + (code & 0x3ff));
        } else {
            if (used + 1 >= capacity) break;
            wide[used++] = code < 0x10000 ? (WCHAR)code : REPLACEMENT_CHARACTER;
        }
    }
    wide[used] = 0;
    return used;
}
