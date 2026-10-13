#include "path.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char path[JOLT_PATH_CAPACITY];
    assert(jolt_resolve_path("/", path, sizeof path) == 0 && !path[0]);
    assert(jolt_resolve_path("/notes.txt", path, sizeof path) == 0 && !strcmp(path, "notes.txt"));
    assert(jolt_resolve_path("./folder//file", path, sizeof path) == 0 && !strcmp(path, "folder\\file"));
    assert(jolt_resolve_path("/folder/../notes.txt", path, sizeof path) == 0 && !strcmp(path, "notes.txt"));
    assert(jolt_resolve_path("/folder/..", path, sizeof path) == 0 && !path[0]);
    assert(jolt_resolve_path("../Windows", path, sizeof path) == -13);
    assert(jolt_resolve_path("/folder/../../Windows", path, sizeof path) == -13);
    assert(jolt_resolve_path("/folder\\..\\Windows", path, sizeof path) == -13);
    assert(jolt_resolve_path("/notes.txt:stream", path, sizeof path) == -13);
    assert(jolt_resolve_path("/file*", path, sizeof path) == -13);
    assert(jolt_resolve_path("/file.", path, sizeof path) == -13);
    assert(jolt_resolve_path("/file ", path, sizeof path) == -13);
    assert(jolt_resolve_path("/\xc3\xa9", path, sizeof path) == -84);
    assert(jolt_resolve_path("", path, sizeof path) == -2);
    assert(jolt_resolve_path("four", path, 4) == -36);
    assert(jolt_absolute_path("/", "notes.txt", path, sizeof path) == 0 && !strcmp(path, "/notes.txt"));
    assert(jolt_absolute_path("/tmp", "a/./b//c", path, sizeof path) == 0 && !strcmp(path, "/tmp/a/b/c"));
    assert(jolt_absolute_path("/tmp", "../etc/profile", path, sizeof path) == 0 && !strcmp(path, "/etc/profile"));
    assert(jolt_absolute_path("/tmp", "/../../Windows", path, sizeof path) == 0 && !strcmp(path, "/Windows"));
    assert(jolt_absolute_path("/tmp", "..", path, sizeof path) == 0 && !strcmp(path, "/"));
    assert(jolt_absolute_path("/", "/", path, sizeof path) == 0 && !strcmp(path, "/"));
    assert(jolt_absolute_path("/", "a\\b", path, sizeof path) == -2);
    assert(jolt_absolute_path("/", "", path, sizeof path) == -2);
    assert(jolt_absolute_path("/", "abcdef", path, 6) == -36);
    puts("PASS: bounded CF paths and root-escape rejection");
    return 0;
}