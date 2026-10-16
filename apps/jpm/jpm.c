#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "sha256.h"

#define STATE "/var/lib/jpm"
#define INDEX STATE "/index"
#define SESSIONS STATE "/tls-sessions"
#define CONFIG "/etc/jpm.conf"
#define TAR_BLOCK 512
#define PATH_CAPACITY 1024
#define LINE_CAPACITY 1024
#define COPY_CAPACITY 8192
#define HEX_DIGEST (SHA256_BLOCK_SIZE * 2 + 1)
#define MAX_ARGUMENTS 256

struct entry {
    char *key, *value;
};

struct package {
    struct entry *entries;
    int entry_count;
};

struct catalogue {
    char *text;
    struct package *packages;
    int count;
};

struct path_list {
    char **paths;
    int count, capacity;
};

struct owner {
    char *path, *name;
};

enum { DOWNLOADS, STAGE, TEMPORARY_SLOTS };

static char *temporary[TEMPORARY_SLOTS];

static void remove_tree(const char *path);

static void release_temporary(int slot) {
    if (!temporary[slot]) return;
    remove_tree(temporary[slot]);
    free(temporary[slot]);
    temporary[slot] = NULL;
}

static void cleanup(void) {
    for (int slot = 0; slot < TEMPORARY_SLOTS; slot++) release_temporary(slot);
}

static void fail(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    fputs("jpm: ", stderr);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);
    va_end(arguments);
    cleanup();
    exit(1);
}

static void *allocate(size_t size) {
    void *memory = calloc(1, size ? size : 1);
    if (!memory) fail("out of memory");
    return memory;
}

static void *grow(void *memory, size_t size) {
    memory = realloc(memory, size);
    if (!memory) fail("out of memory");
    return memory;
}

static char *copy_string(const char *text) {
    char *copy = allocate(strlen(text) + 1);
    strcpy(copy, text);
    return copy;
}

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    size_t length = 0, capacity = COPY_CAPACITY;
    char *text = allocate(capacity + 1);
    size_t got;
    while ((got = fread(text + length, 1, capacity - length, file)) > 0) {
        length += got;
        if (length == capacity) {
            capacity *= 2;
            text = grow(text, capacity + 1);
        }
    }
    fclose(file);
    text[length] = 0;
    return text;
}

static int exists(const char *path) {
    struct stat status;
    return lstat(path, &status) == 0;
}

static struct catalogue parse(char *text) {
    struct catalogue catalogue = { text, NULL, 0 };
    struct package *current = NULL;
    char *line = text;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *separator = strstr(line, " = ");
        if (separator) {
            *separator = 0;
            char *key = line, *value = separator + 3;
            if (!strcmp(key, "pkgname")) {
                catalogue.packages = grow(catalogue.packages, (catalogue.count + 1) * sizeof *catalogue.packages);
                current = &catalogue.packages[catalogue.count++];
                current->entries = NULL;
                current->entry_count = 0;
            }
            if (current) {
                current->entries = grow(current->entries, (current->entry_count + 1) * sizeof *current->entries);
                current->entries[current->entry_count].key = key;
                current->entries[current->entry_count++].value = value;
            }
        }
        line = next;
    }
    return catalogue;
}

static const char *value(const struct package *package, const char *key) {
    for (int index = 0; index < package->entry_count; index++)
        if (!strcmp(package->entries[index].key, key)) return package->entries[index].value;
    return "";
}

static const char *name_of(const struct package *package) {
    return value(package, "pkgname");
}

static struct package *find(struct catalogue *catalogue, const char *name) {
    for (int index = 0; index < catalogue->count; index++)
        if (!strcmp(name_of(&catalogue->packages[index]), name)) return &catalogue->packages[index];
    return NULL;
}

static struct catalogue load_index(void) {
    char *text = read_file(INDEX);
    if (!text) fail("no package list, run 'jpm update'");
    return parse(text);
}

static char *state_path(const char *name, const char *file) {
    char path[PATH_CAPACITY];
    snprintf(path, sizeof path, STATE "/%s/%s", name, file);
    return copy_string(path);
}

static int is_installed(const char *name) {
    char *path = state_path(name, "PKGINFO");
    int found = exists(path);
    free(path);
    return found;
}

static char *installed_version(const char *name) {
    char *path = state_path(name, "PKGINFO");
    char *text = read_file(path);
    free(path);
    if (!text) return copy_string("");
    struct catalogue catalogue = parse(text);
    char *version = copy_string(catalogue.count ? value(&catalogue.packages[0], "pkgver") : "");
    free(catalogue.packages ? catalogue.packages[0].entries : NULL);
    free(catalogue.packages);
    free(text);
    return version;
}

static int compare_strings(const void *first, const void *second) {
    return strcmp(*(char *const *)first, *(char *const *)second);
}

static struct path_list installed_names(void) {
    struct path_list names = { NULL, 0, 0 };
    DIR *directory = opendir(STATE);
    if (!directory) return names;
    struct dirent *item;
    while ((item = readdir(directory))) {
        if (item->d_name[0] == '.' || !is_installed(item->d_name)) continue;
        names.paths = grow(names.paths, (names.count + 1) * sizeof *names.paths);
        names.paths[names.count++] = copy_string(item->d_name);
    }
    closedir(directory);
    if (names.count) qsort(names.paths, names.count, sizeof *names.paths, compare_strings);
    return names;
}

static void add_path(struct path_list *list, const char *path) {
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 16;
        list->paths = grow(list->paths, list->capacity * sizeof *list->paths);
    }
    list->paths[list->count++] = copy_string(path);
}

static struct path_list read_lines(const char *path) {
    struct path_list list = { NULL, 0, 0 };
    char *text = read_file(path);
    if (!text) return list;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) add_path(&list, line);
    free(text);
    return list;
}

static int abi(void) {
    FILE *file = fopen("/proc/jolt", "r");
    int version = 1;
    char line[LINE_CAPACITY];
    while (file && fgets(line, sizeof line, file))
        if (!strncmp(line, "abi: ", 5)) version = atoi(line + 5);
    if (file) fclose(file);
    return version;
}

static char *repository(void) {
    const char *configured = getenv("JPM_REPO");
    if (configured && *configured) return copy_string(configured);
    FILE *file = fopen(CONFIG, "r");
    char line[LINE_CAPACITY];
    char *found = NULL;
    while (!found && file && fgets(line, sizeof line, file)) {
        char url[LINE_CAPACITY];
        if (sscanf(line, "repo %1023s", url) == 1) found = copy_string(url);
    }
    if (file) fclose(file);
    if (!found) fail("no repository set in " CONFIG);
    return found;
}

static char *abi_url(const char *repo, const char *file) {
    char url[PATH_CAPACITY];
    snprintf(url, sizeof url, "%s/abi%d/%s", repo, abi(), file);
    return copy_string(url);
}

static int is_remote(const char *location) {
    return !strncmp(location, "http://", 7) || !strncmp(location, "https://", 8);
}

static int run(char *const arguments[]) {
    fflush(stdout);
    pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        execvp(arguments[0], arguments);
        _exit(127);
    }
    int status;
    while (waitpid(child, &status, 0) < 0)
        if (errno != EINTR) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int download(const char *first_url, char **pairs, int pair_count, int progress) {
    const char *host = strstr(first_url, "://") + 3;
    printf("jpm: connecting to %.*s, this can take a minute\n", (int)strcspn(host, "/"), host);
    mkdir(STATE, 0755);
    char *arguments[MAX_ARGUMENTS];
    int count = 0;
    char *fixed[] = { "curl", "-fSL", "--connect-timeout", "60", "--max-time", "900", "--ssl-sessions", SESSIONS };
    for (size_t index = 0; index < sizeof fixed / sizeof *fixed; index++) arguments[count++] = fixed[index];
    arguments[count++] = progress ? "-#" : "-sS";
    for (int index = 0; index < pair_count * 2 && count < MAX_ARGUMENTS - 1; index += 2) {
        arguments[count++] = "-o";
        arguments[count++] = pairs[index];
        arguments[count++] = pairs[index + 1];
    }
    arguments[count] = NULL;
    return run(arguments);
}

static int copy_file(const char *source, const char *destination) {
    FILE *input = fopen(source, "rb");
    if (!input) return -1;
    FILE *output = fopen(destination, "wb");
    if (!output) {
        fclose(input);
        return -1;
    }
    char buffer[COPY_CAPACITY];
    size_t got;
    int result = 0;
    while ((got = fread(buffer, 1, sizeof buffer, input)) > 0)
        if (fwrite(buffer, 1, got, output) != got) result = -1;
    fclose(input);
    if (fclose(output)) result = -1;
    return result;
}

static int fetch(const char *location, const char *destination) {
    if (!is_remote(location)) return copy_file(location, destination);
    char *pair[] = { (char *)destination, (char *)location };
    return download(location, pair, 1, 0);
}

static void sha256_file(const char *path, char hex[HEX_DIGEST]) {
    FILE *file = fopen(path, "rb");
    hex[0] = 0;
    if (!file) return;
    SHA256_CTX context;
    sha256_init(&context);
    BYTE buffer[COPY_CAPACITY], digest[SHA256_BLOCK_SIZE];
    size_t got;
    while ((got = fread(buffer, 1, sizeof buffer, file)) > 0) sha256_update(&context, buffer, got);
    fclose(file);
    sha256_final(&context, digest);
    for (int index = 0; index < SHA256_BLOCK_SIZE; index++) sprintf(hex + index * 2, "%02x", digest[index]);
}

static void make_parents(const char *path) {
    char partial[PATH_CAPACITY];
    snprintf(partial, sizeof partial, "%s", path);
    for (char *slash = strchr(partial + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = 0;
        mkdir(partial, 0755);
        *slash = '/';
    }
}

static void remove_tree(const char *path) {
    DIR *directory = opendir(path);
    if (directory) {
        struct dirent *item;
        while ((item = readdir(directory))) {
            if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
            char child[PATH_CAPACITY];
            snprintf(child, sizeof child, "%s/%s", path, item->d_name);
            remove_tree(child);
        }
        closedir(directory);
        rmdir(path);
    } else {
        unlink(path);
    }
}

static char *make_temporary(int slot) {
    char template[] = "/tmp/jpm.XXXXXX";
    if (!mkdtemp(template)) fail("could not make a working directory in /tmp");
    temporary[slot] = copy_string(template);
    return temporary[slot];
}

static unsigned long octal(const char *field, int length) {
    unsigned long result = 0;
    for (int index = 0; index < length && field[index]; index++)
        if (field[index] >= '0' && field[index] <= '7') result = result * 8 + (unsigned long)(field[index] - '0');
    return result;
}

static int read_exact(FILE *input, void *buffer, size_t length) {
    return fread(buffer, 1, length, input) == length;
}

static char *read_member(FILE *input, unsigned long size) {
    char *data = allocate(size + 1);
    if (!read_exact(input, data, size)) fail("package is truncated");
    unsigned long padding = (TAR_BLOCK - size % TAR_BLOCK) % TAR_BLOCK;
    char skip[TAR_BLOCK];
    if (padding && !read_exact(input, skip, padding)) fail("package is truncated");
    return data;
}

static char *pax_path(const char *records) {
    const char *cursor = records;
    while (*cursor) {
        char *end;
        long length = strtol(cursor, &end, 10);
        if (length <= 0 || *end != ' ') break;
        const char *record = end + 1;
        if (!strncmp(record, "path=", 5)) {
            const char *start = record + 5, *stop = cursor + length - 1;
            char *path = allocate((size_t)(stop - start) + 1);
            memcpy(path, start, (size_t)(stop - start));
            return path;
        }
        cursor += length;
    }
    return NULL;
}

static int unsafe_path(const char *path) {
    if (path[0] == '/') return 1;
    for (const char *part = path; part; part = strchr(part, '/') ? strchr(part, '/') + 1 : NULL)
        if (part[0] == '.' && part[1] == '.' && (part[2] == '/' || part[2] == 0)) return 1;
    return 0;
}

static FILE *open_gzip(const char *path, pid_t *child) {
    int pipe_ends[2];
    if (pipe(pipe_ends)) return NULL;
    fflush(stdout);
    *child = fork();
    if (*child < 0) return NULL;
    if (*child == 0) {
        dup2(pipe_ends[1], 1);
        close(pipe_ends[0]);
        close(pipe_ends[1]);
        execlp("gzip", "gzip", "-dc", path, (char *)NULL);
        _exit(127);
    }
    close(pipe_ends[1]);
    return fdopen(pipe_ends[0], "rb");
}

static char *unpack(const char *package, const char *stage, struct path_list *files) {
    pid_t child;
    FILE *input = open_gzip(package, &child);
    if (!input) fail("could not unpack %s", package);
    char *info = NULL, *long_name = NULL;
    unsigned char header[TAR_BLOCK];
    while (read_exact(input, header, sizeof header)) {
        if (!header[0]) break;
        unsigned long size = octal((char *)header + 124, 12);
        char type = (char)header[156];
        if (type == 'L' || type == 'x') {
            char *data = read_member(input, size);
            free(long_name);
            long_name = type == 'L' ? data : pax_path(data);
            if (type == 'x') free(data);
            continue;
        }
        if (type == 'g') {
            free(read_member(input, size));
            continue;
        }
        char name[PATH_CAPACITY];
        if (long_name) {
            snprintf(name, sizeof name, "%s", long_name);
            free(long_name);
            long_name = NULL;
        } else if (!memcmp(header + 257, "ustar", 6) && header[345]) {
            snprintf(name, sizeof name, "%.155s/%.100s", (char *)header + 345, (char *)header);
        } else {
            snprintf(name, sizeof name, "%.100s", (char *)header);
        }
        char *relative = name;
        while (relative[0] == '.' && relative[1] == '/') relative += 2;
        if (unsafe_path(relative)) fail("%s has files outside the package", package);
        if (type == '5' || !*relative) {
            free(read_member(input, size));
            continue;
        }
        if (type != '0' && type != 0) fail("%s has links or special files, which JoLT does not support", package);
        char *data = read_member(input, size);
        if (!strcmp(relative, ".PKGINFO")) {
            free(info);
            info = data;
            continue;
        }
        char destination[PATH_CAPACITY];
        snprintf(destination, sizeof destination, "%s/%s", stage, relative);
        make_parents(destination);
        FILE *output = fopen(destination, "wb");
        if (!output || fwrite(data, 1, size, output) != size || fclose(output)) fail("could not unpack %s", relative);
        chmod(destination, (mode_t)octal((char *)header + 100, 8) & 0777);
        free(data);
        char installed[PATH_CAPACITY];
        snprintf(installed, sizeof installed, "/%s", relative);
        add_path(files, installed);
    }
    while (read_exact(input, header, sizeof header)) {}
    fclose(input);
    int status;
    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) fail("could not unpack %s", package);
    if (!info) fail("%s is not a package", package);
    if (files->count) qsort(files->paths, files->count, sizeof *files->paths, compare_strings);
    return info;
}

static int contains(const struct path_list *list, const char *path) {
    return list->count && bsearch(&path, list->paths, list->count, sizeof *list->paths, compare_strings) != NULL;
}

static int compare_owners(const void *first, const void *second) {
    return strcmp(((const struct owner *)first)->path, ((const struct owner *)second)->path);
}

static struct owner *load_owners(int *count) {
    struct path_list names = installed_names();
    struct owner *owners = NULL;
    *count = 0;
    for (int index = 0; index < names.count; index++) {
        char *list_path = state_path(names.paths[index], "files");
        struct path_list files = read_lines(list_path);
        free(list_path);
        owners = grow(owners, (*count + files.count + 1) * sizeof *owners);
        for (int file = 0; file < files.count; file++) {
            owners[*count].path = files.paths[file];
            owners[(*count)++].name = names.paths[index];
        }
        free(files.paths);
    }
    if (*count) qsort(owners, *count, sizeof *owners, compare_owners);
    return owners;
}

static const char *owner_of(struct owner *owners, int count, const char *path) {
    struct owner key = { (char *)path, NULL };
    struct owner *found = count ? bsearch(&key, owners, count, sizeof *owners, compare_owners) : NULL;
    return found ? found->name : NULL;
}

struct sums {
    char **paths, **digests;
    int count;
};

static struct sums read_sums(const char *name) {
    struct sums sums = { NULL, NULL, 0 };
    char *path = state_path(name, "sha256sums");
    struct path_list lines = read_lines(path);
    free(path);
    sums.paths = allocate((lines.count + 1) * sizeof *sums.paths);
    sums.digests = allocate((lines.count + 1) * sizeof *sums.digests);
    for (int index = 0; index < lines.count; index++) {
        char *separator = strstr(lines.paths[index], "  ");
        if (!separator) continue;
        *separator = 0;
        sums.digests[sums.count] = lines.paths[index];
        sums.paths[sums.count++] = separator + 2;
    }
    return sums;
}

static const char *recorded_sum(const struct sums *sums, const char *path) {
    for (int index = 0; index < sums->count; index++)
        if (!strcmp(sums->paths[index], path)) return sums->digests[index];
    return NULL;
}

static int changed_since_install(const struct sums *sums, const char *path) {
    const char *recorded = recorded_sum(sums, path);
    if (!recorded || !exists(path)) return 0;
    char actual[HEX_DIGEST];
    sha256_file(path, actual);
    return strcmp(actual, recorded) != 0;
}

static void write_lines(const char *path, const struct path_list *list) {
    FILE *file = fopen(path, "w");
    if (!file) fail("could not write %s", path);
    for (int index = 0; index < list->count; index++) fprintf(file, "%s\n", list->paths[index]);
    fclose(file);
}

static void move_into_place(const char *stage, const char *path) {
    char source[PATH_CAPACITY];
    snprintf(source, sizeof source, "%s%s", stage, path);
    make_parents(path);
    if (rename(source, path) == 0) return;
    struct stat status;
    if (copy_file(source, path) || stat(source, &status)) fail("could not copy %s into place", path);
    chmod(path, status.st_mode & 0777);
}

static void install_file(const char *package_path) {
    char stage[PATH_CAPACITY];
    snprintf(stage, sizeof stage, "%s/stage", make_temporary(STAGE));
    mkdir(stage, 0755);
    struct path_list files = { NULL, 0, 0 };
    char *info_text = unpack(package_path, stage, &files);
    char *info_copy = copy_string(info_text);
    struct catalogue info = parse(info_text);
    if (!info.count) fail("%s has no pkgname", package_path);
    struct package *package = &info.packages[0];
    const char *name = name_of(package), *version = value(package, "pkgver"), *arch = value(package, "arch");
    char expected_arch[32];
    snprintf(expected_arch, sizeof expected_arch, "jolt%d", abi());
    if (strcmp(arch, "any") && strcmp(arch, expected_arch)) fail("%s is for %s, this JoLT runs %s", name, arch, expected_arch);
    for (int index = 0; index < package->entry_count; index++)
        if (!strcmp(package->entries[index].key, "depend") && !is_installed(package->entries[index].value))
            fail("%s needs %s", name, package->entries[index].value);
    printf("jpm: checking %s %s\n", name, version);
    int owner_count;
    struct owner *owners = load_owners(&owner_count);
    for (int index = 0; index < files.count; index++) {
        const char *owner = owner_of(owners, owner_count, files.paths[index]);
        if (owner && strcmp(owner, name)) fail("%s belongs to %s", files.paths[index], owner);
        if (!owner && exists(files.paths[index])) fail("%s already exists", files.paths[index]);
    }
    if (files.count == 1) printf("jpm: installing %s (1 file)\n", name);
    else printf("jpm: installing %s (%d files)\n", name, files.count);
    char **digests = allocate((files.count + 1) * sizeof *digests);
    for (int index = 0; index < files.count; index++) {
        char staged[PATH_CAPACITY];
        snprintf(staged, sizeof staged, "%s%s", stage, files.paths[index]);
        digests[index] = allocate(HEX_DIGEST);
        sha256_file(staged, digests[index]);
    }
    struct sums old_sums = read_sums(name);
    for (int index = 0; index < files.count; index++) {
        const char *path = files.paths[index];
        char current[HEX_DIGEST];
        if (changed_since_install(&old_sums, path) && (sha256_file(path, current), strcmp(current, digests[index]))) {
            char kept[PATH_CAPACITY], staged[PATH_CAPACITY];
            snprintf(kept, sizeof kept, "%s.jpmnew", path);
            snprintf(staged, sizeof staged, "%s%s", stage, path);
            if (rename(staged, kept) && copy_file(staged, kept)) fail("could not write %s", kept);
            printf("jpm: kept your changes to %s, the new version is %s\n", path, kept);
            continue;
        }
        move_into_place(stage, path);
    }
    char *list_path = state_path(name, "files");
    struct path_list old = read_lines(list_path);
    for (int index = 0; index < old.count; index++) {
        if (contains(&files, old.paths[index])) continue;
        if (changed_since_install(&old_sums, old.paths[index])) printf("jpm: kept %s, it was changed\n", old.paths[index]);
        else unlink(old.paths[index]);
    }
    char directory[PATH_CAPACITY];
    snprintf(directory, sizeof directory, STATE "/%s", name);
    make_parents(list_path);
    mkdir(directory, 0755);
    char *info_path = state_path(name, "PKGINFO");
    FILE *file = fopen(info_path, "w");
    if (!file || fputs(info_copy, file) < 0 || fclose(file)) fail("could not record %s", name);
    write_lines(list_path, &files);
    char *sums_path = state_path(name, "sha256sums");
    file = fopen(sums_path, "w");
    if (!file) fail("could not record %s", name);
    for (int index = 0; index < files.count; index++) fprintf(file, "%s  %s\n", digests[index], files.paths[index]);
    fclose(file);
    printf("jpm: installed %s %s\n", name, version);
    release_temporary(STAGE);
}

static void resolve(struct catalogue *catalogue, const char *name, struct path_list *order, struct path_list *visiting) {
    for (int index = 0; index < order->count; index++)
        if (!strcmp(order->paths[index], name)) return;
    for (int index = 0; index < visiting->count; index++)
        if (!strcmp(visiting->paths[index], name)) fail("packages depend on each other: %s", name);
    struct package *package = find(catalogue, name);
    if (!package) fail("no package called %s", name);
    add_path(visiting, name);
    for (int index = 0; index < package->entry_count; index++) {
        const char *dependency = package->entries[index].value;
        if (!strcmp(package->entries[index].key, "depend") && !is_installed(dependency))
            resolve(catalogue, dependency, order, visiting);
    }
    add_path(order, name);
}

static void install_from_repository(struct catalogue *catalogue, struct path_list *order) {
    char *repo = repository();
    char *work = make_temporary(DOWNLOADS);
    char **pairs = allocate(order->count * 2 * sizeof *pairs);
    char *first_url = NULL;
    size_t names_length = 1;
    for (int index = 0; index < order->count; index++) names_length += strlen(order->paths[index]) + 1;
    char *names = allocate(names_length);
    for (int index = 0; index < order->count; index++) {
        struct package *package = find(catalogue, order->paths[index]);
        const char *filename = value(package, "filename");
        char local[PATH_CAPACITY];
        snprintf(local, sizeof local, "%s/%s", work, filename);
        pairs[index * 2] = copy_string(local);
        pairs[index * 2 + 1] = abi_url(repo, filename);
        if (!first_url) first_url = pairs[index * 2 + 1];
        strcat(names, " ");
        strcat(names, order->paths[index]);
    }
    if (is_remote(repo)) {
        printf("jpm: fetching%s\n", names);
        if (download(first_url, pairs, order->count, 1)) fail("could not fetch%s", names);
    } else {
        for (int index = 0; index < order->count; index++)
            if (copy_file(pairs[index * 2 + 1], pairs[index * 2])) fail("no file %s", pairs[index * 2 + 1]);
    }
    for (int index = 0; index < order->count; index++) {
        struct package *package = find(catalogue, order->paths[index]);
        char actual[HEX_DIGEST];
        sha256_file(pairs[index * 2], actual);
        if (strcmp(actual, value(package, "sha256")))
            fail("%s is damaged or has changed, run 'jpm update'", value(package, "filename"));
    }
    for (int index = 0; index < order->count; index++) install_file(pairs[index * 2]);
    cleanup();
}

static int compare_segments(const char *first, const char *second) {
    for (;;) {
        while (*first && !isalnum((unsigned char)*first)) first++;
        while (*second && !isalnum((unsigned char)*second)) second++;
        if (!*first || !*second) break;
        int numeric = isdigit((unsigned char)*first) != 0;
        if (numeric != (isdigit((unsigned char)*second) != 0)) return numeric ? 1 : -1;
        if (numeric) {
            while (*first == '0') first++;
            while (*second == '0') second++;
        }
        const char *first_start = first, *second_start = second;
        while (*first && (numeric ? isdigit((unsigned char)*first) : isalpha((unsigned char)*first))) first++;
        while (*second && (numeric ? isdigit((unsigned char)*second) : isalpha((unsigned char)*second))) second++;
        size_t first_length = (size_t)(first - first_start), second_length = (size_t)(second - second_start);
        if (numeric && first_length != second_length) return first_length > second_length ? 1 : -1;
        int order = strncmp(first_start, second_start, first_length < second_length ? first_length : second_length);
        if (order) return order > 0 ? 1 : -1;
        if (first_length != second_length) return first_length > second_length ? 1 : -1;
    }
    if (!*first && !*second) return 0;
    if ((!*first && !isalpha((unsigned char)*second)) || isalpha((unsigned char)*first)) return -1;
    return 1;
}

static int compare_versions(const char *first, const char *second) {
    char first_upstream[LINE_CAPACITY], second_upstream[LINE_CAPACITY];
    snprintf(first_upstream, sizeof first_upstream, "%s", first);
    snprintf(second_upstream, sizeof second_upstream, "%s", second);
    char *first_release = strrchr(first_upstream, '-'), *second_release = strrchr(second_upstream, '-');
    if (first_release) *first_release++ = 0;
    if (second_release) *second_release++ = 0;
    int order = compare_segments(first_upstream, second_upstream);
    if (order) return order;
    return compare_segments(first_release ? first_release : "", second_release ? second_release : "");
}

static int ends_with(const char *text, const char *suffix) {
    size_t length = strlen(text), suffix_length = strlen(suffix);
    return length >= suffix_length && !strcmp(text + length - suffix_length, suffix);
}

static void command_install(int count, char **names) {
    if (!count) fail("usage: jpm install PACKAGE...");
    struct catalogue catalogue = { NULL, NULL, 0 };
    int loaded = 0;
    struct path_list order = { NULL, 0, 0 };
    for (int index = 0; index < count; index++) {
        if (ends_with(names[index], ".pkg.tar.gz")) {
            if (!exists(names[index])) fail("no file %s", names[index]);
            install_file(names[index]);
            continue;
        }
        if (!loaded) {
            catalogue = load_index();
            loaded = 1;
        }
        struct package *package = find(&catalogue, names[index]);
        if (package && is_installed(names[index])) {
            char *version = installed_version(names[index]);
            int order = compare_versions(value(package, "pkgver"), version);
            if (order == 0) printf("jpm: %s is already installed\n", names[index]);
            if (order < 0) printf("jpm: %s %s is newer than %s in the repository\n", names[index], version, value(package, "pkgver"));
            free(version);
            if (order <= 0) continue;
        }
        struct path_list visiting = { NULL, 0, 0 };
        resolve(&catalogue, names[index], &order, &visiting);
    }
    if (order.count) install_from_repository(&catalogue, &order);
}

static void command_update(void) {
    char *repo = repository();
    char *url = abi_url(repo, "index");
    mkdir("/var", 0755);
    mkdir("/var/lib", 0755);
    mkdir(STATE, 0755);
    if (fetch(url, INDEX ".new")) {
        unlink(INDEX ".new");
        fail("could not fetch %s", url);
    }
    if (rename(INDEX ".new", INDEX)) fail("could not save the package list");
    struct catalogue catalogue = load_index();
    if (catalogue.count == 1) printf("jpm: 1 package available\n");
    else printf("jpm: %d packages available\n", catalogue.count);
}

static void command_list(void) {
    struct catalogue catalogue = load_index();
    for (int index = 0; index < catalogue.count; index++) {
        struct package *package = &catalogue.packages[index];
        char label[LINE_CAPACITY];
        snprintf(label, sizeof label, "%s %s", name_of(package), value(package, "pkgver"));
        printf("%s %-20s %s\n", is_installed(name_of(package)) ? "*" : " ", label, value(package, "pkgdesc"));
    }
}

static int matches(const char *text, const char *wanted) {
    size_t length = strlen(wanted);
    for (; *text; text++)
        if (!strncasecmp(text, wanted, length)) return 1;
    return 0;
}

static void command_search(int count, char **words) {
    if (!count) fail("usage: jpm search TEXT");
    struct catalogue catalogue = load_index();
    for (int index = 0; index < catalogue.count; index++) {
        struct package *package = &catalogue.packages[index];
        if (matches(name_of(package), words[0]) || matches(value(package, "pkgdesc"), words[0]))
            printf("%-20s %s\n", name_of(package), value(package, "pkgdesc"));
    }
}

static void command_info(int count, char **names) {
    if (!count) fail("usage: jpm info PACKAGE");
    char *text = read_file(INDEX);
    struct catalogue catalogue = text ? parse(text) : (struct catalogue){ NULL, NULL, 0 };
    struct package *package = find(&catalogue, names[0]);
    if (!package && is_installed(names[0])) {
        char *path = state_path(names[0], "PKGINFO");
        catalogue = parse(read_file(path));
        free(path);
        package = catalogue.count ? &catalogue.packages[0] : NULL;
    }
    if (!package) fail("no package called %s", names[0]);
    for (int index = 0; index < package->entry_count; index++) {
        const struct entry *entry = &package->entries[index];
        if (strcmp(entry->key, "sha256") && strcmp(entry->key, "filename")) printf("%s = %s\n", entry->key, entry->value);
    }
    if (is_installed(names[0])) printf("installed = %s\n", installed_version(names[0]));
}

static void command_installed(void) {
    struct path_list names = installed_names();
    for (int index = 0; index < names.count; index++) printf("%-20s %s\n", names.paths[index], installed_version(names.paths[index]));
}

static void command_files(int count, char **names) {
    if (!count) fail("usage: jpm files PACKAGE");
    if (!is_installed(names[0])) fail("%s is not installed", names[0]);
    char *path = state_path(names[0], "files");
    struct path_list files = read_lines(path);
    for (int index = 0; index < files.count; index++) puts(files.paths[index]);
}

static int depends_on(const char *other, const char *name) {
    char *path = state_path(other, "PKGINFO");
    char *text = read_file(path);
    free(path);
    if (!text) return 0;
    struct catalogue catalogue = parse(text);
    int found = 0;
    for (int index = 0; catalogue.count && index < catalogue.packages[0].entry_count; index++) {
        const struct entry *entry = &catalogue.packages[0].entries[index];
        if (!strcmp(entry->key, "depend") && !strcmp(entry->value, name)) found = 1;
    }
    return found;
}

static int listed(int count, char **names, const char *name) {
    for (int index = 0; index < count; index++)
        if (!strcmp(names[index], name)) return 1;
    return 0;
}

static void command_remove(int count, char **names) {
    if (!count) fail("usage: jpm remove PACKAGE...");
    struct path_list installed = installed_names();
    for (int index = 0; index < count; index++) {
        if (!is_installed(names[index])) fail("%s is not installed", names[index]);
        for (int other = 0; other < installed.count; other++) {
            const char *candidate = installed.paths[other];
            if (!listed(count, names, candidate) && depends_on(candidate, names[index]))
                fail("%s needs %s", candidate, names[index]);
        }
    }
    for (int index = 0; index < count; index++) {
        char *path = state_path(names[index], "files");
        struct path_list files = read_lines(path);
        struct sums sums = read_sums(names[index]);
        for (int file = 0; file < files.count; file++) {
            if (changed_since_install(&sums, files.paths[file])) {
                printf("jpm: kept %s, it was changed\n", files.paths[file]);
                continue;
            }
            unlink(files.paths[file]);
            char directory[PATH_CAPACITY];
            snprintf(directory, sizeof directory, "%s", files.paths[file]);
            for (char *slash = strrchr(directory, '/'); slash && slash != directory; slash = strrchr(directory, '/')) {
                *slash = 0;
                if (rmdir(directory)) break;
            }
        }
        char directory[PATH_CAPACITY];
        snprintf(directory, sizeof directory, STATE "/%s", names[index]);
        remove_tree(directory);
        printf("jpm: removed %s\n", names[index]);
    }
}

static void command_upgrade(void) {
    struct catalogue catalogue = load_index();
    struct path_list names = installed_names();
    char **outdated = allocate((names.count + 1) * sizeof *outdated);
    int count = 0;
    for (int index = 0; index < names.count; index++) {
        struct package *package = find(&catalogue, names.paths[index]);
        char *version = installed_version(names.paths[index]);
        if (package && compare_versions(value(package, "pkgver"), version) > 0) outdated[count++] = names.paths[index];
        free(version);
    }
    if (!count) {
        printf("jpm: everything is up to date\n");
        return;
    }
    command_install(count, outdated);
}

static void command_verify(int count, char **names) {
    struct path_list installed = installed_names();
    if (!count) {
        count = installed.count;
        names = installed.paths;
    }
    int problems = 0;
    for (int index = 0; index < count; index++) {
        if (!is_installed(names[index])) fail("%s is not installed", names[index]);
        struct sums sums = read_sums(names[index]);
        if (!sums.count) {
            printf("jpm: %s has no checksums, reinstall it to add them\n", names[index]);
            continue;
        }
        int bad = 0;
        for (int file = 0; file < sums.count; file++) {
            if (!exists(sums.paths[file])) {
                printf("jpm: %s is missing %s\n", names[index], sums.paths[file]);
                bad++;
            } else if (changed_since_install(&sums, sums.paths[file])) {
                printf("jpm: %s has a changed %s\n", names[index], sums.paths[file]);
                bad++;
            }
        }
        if (!bad) printf("jpm: %s is intact\n", names[index]);
        problems += bad;
    }
    if (problems) exit(1);
}

static void usage(FILE *output) {
    fputs("usage: jpm COMMAND [PACKAGE...]\n"
        "\n"
        "  update            fetch the list of packages\n"
        "  list              list available packages, * marks installed\n"
        "  installed         list installed packages\n"
        "  search TEXT       find packages by name or description\n"
        "  info PACKAGE      describe a package\n"
        "  install PACKAGE   install packages and what they need, or a .pkg.tar.gz file\n"
        "  remove PACKAGE    remove packages\n"
        "  upgrade           install newer versions of installed packages\n"
        "  files PACKAGE     list the files a package installed\n"
        "  verify [PACKAGE]  check installed files against their checksums\n", output);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc < 2) {
        usage(stdout);
        return 1;
    }
    const char *command = argv[1];
    int count = argc - 2;
    char **rest = argv + 2;
    if (!strcmp(command, "update")) command_update();
    else if (!strcmp(command, "list")) command_list();
    else if (!strcmp(command, "installed")) command_installed();
    else if (!strcmp(command, "search")) command_search(count, rest);
    else if (!strcmp(command, "info")) command_info(count, rest);
    else if (!strcmp(command, "install")) command_install(count, rest);
    else if (!strcmp(command, "remove") || !strcmp(command, "uninstall")) command_remove(count, rest);
    else if (!strcmp(command, "upgrade")) command_upgrade();
    else if (!strcmp(command, "files")) command_files(count, rest);
    else if (!strcmp(command, "verify")) command_verify(count, rest);
    else if (!strcmp(command, "help") || !strcmp(command, "-h") || !strcmp(command, "--help")) usage(stdout);
    else {
        fprintf(stderr, "jpm: unknown command %s\n", command);
        usage(stderr);
        return 1;
    }
    return 0;
}
