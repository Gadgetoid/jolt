#include "path.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define PATH_CAPACITY 256

int main(void) {
    char path[PATH_CAPACITY];
    assert(jolt_absolute_path("/", "notes.txt", path, sizeof path) == 0 && !strcmp(path, "/notes.txt"));
    assert(jolt_absolute_path("/tmp", "a/./b//c", path, sizeof path) == 0 && !strcmp(path, "/tmp/a/b/c"));
    assert(jolt_absolute_path("/tmp", "../etc/profile", path, sizeof path) == 0 && !strcmp(path, "/etc/profile"));
    assert(jolt_absolute_path("/tmp", "/../../Windows", path, sizeof path) == 0 && !strcmp(path, "/Windows"));
    assert(jolt_absolute_path("/tmp", "..", path, sizeof path) == 0 && !strcmp(path, "/"));
    assert(jolt_absolute_path("/", "/", path, sizeof path) == 0 && !strcmp(path, "/"));
    assert(jolt_absolute_path("/", "a\\b", path, sizeof path) == -2);
    assert(jolt_absolute_path("/", "", path, sizeof path) == -2);
    assert(jolt_absolute_path("/", "abcdef", path, 6) == -36);
    assert(jolt_absolute_path("/", "notes.txt:stream", path, sizeof path) == -2);
    assert(jolt_absolute_path("/", "file*", path, sizeof path) == -2);
    assert(jolt_absolute_path("/", "file.", path, sizeof path) == -2);
    assert(jolt_absolute_path("/", "file ", path, sizeof path) == -2);
    assert(jolt_absolute_path("/", "\xc3\xa9", path, sizeof path) == -84);
    assert(jolt_absolute_path("/tmp", "../../..", path, sizeof path) == 0 && !strcmp(path, "/"));
    puts("PASS: bounded CF paths and root-escape rejection");
    return 0;
}