/* fs.c - filesystem, path, and wildcard helpers. */

#include "cbuild_internal.h"

static void remove_dir_recursive(cbuild_context_t* ctx, const char* path);

/* Normalize path separators to forward slashes for consistent hashing/comparison */
CBUILD_INTERNAL char* cbuild__normalize_path(const char* path) {
    if (!path) return NULL;
    char* normalized = strdup(path);
    if (!normalized) return NULL;
#ifdef _WIN32
    for (char* p = normalized; *p; ++p) {
        if (*p == '\\') *p = '/';
    }
#endif
    return normalized;
}

/* Returns 0 and fills *out, or -1 when the path cannot be examined. */
CBUILD_INTERNAL int cbuild__mtime(const char* path, cbuild_mtime_t* out) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) return -1;
    *out = ((cbuild_mtime_t)data.ftLastWriteTime.dwHighDateTime << 32) |
           (cbuild_mtime_t)data.ftLastWriteTime.dwLowDateTime;
    return 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    cbuild_mtime_t nsec = 0;
    /* The sub-second field has no portable name. Where st_mtime is a macro it
     * aliases the seconds of a timespec member, which tells the layouts apart
     * even when this header is not the first include. */
#if defined(st_mtime) && defined(__APPLE__)
    nsec = (cbuild_mtime_t)st.st_mtimespec.tv_nsec;
#elif defined(st_mtime)
    nsec = (cbuild_mtime_t)st.st_mtim.tv_nsec;
#elif defined(__APPLE__) || defined(__GLIBC__)
    nsec = (cbuild_mtime_t)st.st_mtimensec;
#endif
    *out = (cbuild_mtime_t)st.st_mtime * 1000000000ULL + nsec;
    return 0;
#endif
}

int cbuild_file_exists(const char* path) {
    if (!path || !*path)
        return 0;
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int cbuild_dir_exists(const char* path) {
    if (!path || !*path)
        return 0;
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int cbuild_remove_file(const char* path) {
    if (!path || !*path)
        return -1;
    if (!cbuild_file_exists(path))
        return 0;
    return unlink(path);
}

int cbuild_remove_dir(const char* path) {
    if (!path || !*path)
        return -1;
    if (!cbuild_dir_exists(path))
        return 0;
#ifdef _WIN32
    char cmd[PATH_MAX + 32];
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\"", path);
    return system(cmd);
#else
    DIR* dir;
    struct dirent* entry;
    char full_path[PATH_MAX];
    if (!(dir = opendir(path)))
        return -1;
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (cbuild_remove_dir(full_path) != 0) {
                closedir(dir);
                return -1;
            }
        } else {
            if (cbuild_remove_file(full_path) != 0) {
                closedir(dir);
                return -1;
            }
        }
    }
    closedir(dir);
    return rmdir(path);
#endif
}

int cbuild_get_cwd(char* buf, long size) {
    if (!buf || size <= 0)
        return -1;
    return getcwd(buf, size) ? 0 : -1;
}

int cbuild_match_wildcard(const char* pattern, const char* string) {
    if (!pattern || !string)
        return 0;

    if (*pattern == '\0')
        return *string == '\0';

    if (*pattern == '*') {
        if (*(pattern + 1) == '*' && *(pattern + 2) != '*')
            pattern++;

        if (*(pattern + 1) == '\0')
            return 1;

        while (*string) {
            if (cbuild_match_wildcard(pattern + 1, string))
                return 1;
            string++;
        }
        return cbuild_match_wildcard(pattern + 1, string);
    }

    if (*pattern == '?' || *pattern == *string) {
        return cbuild_match_wildcard(pattern + 1, string + 1);
    }

    return 0;
}

int cbuild_expand_wildcard(const char* pattern, char*** files,
                           int* file_count) {
    char dir_path[PATH_MAX] = { 0 };
    char base_pattern[PATH_MAX] = { 0 };
    int capacity = 32;

    if (!pattern || !files || !file_count)
        return -1;

    *files = NULL;
    *file_count = 0;

    const char* last_slash = strrchr(pattern, '/');
    const char* last_backslash = strrchr(pattern, '\\');
    const char* separator = last_slash ? last_slash : last_backslash;

    if (!separator) {
        strcpy(dir_path, ".");
        strcpy(base_pattern, pattern);
    } else {
        size_t dir_len = separator - pattern;
        strncpy(dir_path, pattern, dir_len);
        dir_path[dir_len] = '\0';

        strcpy(base_pattern, separator + 1);
    }

    *files = (char**)malloc(capacity * sizeof(char*));
    if (!*files)
        return -1;

    int result = cbuild_expand_wildcard_recursive(dir_path, base_pattern, files,
                                                  file_count, &capacity);

    if (result != 0 && *files) {
        for (int i = 0; i < *file_count; i++) {
            free((*files)[i]);
        }
        free(*files);
        *files = NULL;
        *file_count = 0;
    }

    return result;
}

static int cbuild_add_to_file_list(char*** files, int* file_count,
                                   int* capacity, const char* path) {
    if (*file_count >= *capacity) {
        *capacity *= 2;
        char** new_files = (char**)realloc(*files, *capacity * sizeof(char*));
        if (!new_files) {
            return -1;
        }
        *files = new_files;
    }

    /* Normalize path separators for consistent hashing on Windows */
    (*files)[*file_count] = cbuild__normalize_path(path);
    if (!(*files)[*file_count]) {
        return -1;
    }
    (*file_count)++;

    return 0;
}

int cbuild_expand_wildcard_recursive(const char* dir_path, const char* pattern,
                                     char*** files, int* file_count,
                                     int* capacity) {
    int recursive_search = 0;
    char next_pattern[PATH_MAX] = { 0 };

    if (strncmp(pattern, "**", 2) == 0) {
        recursive_search = 1;

        if (pattern[2] == '/' || pattern[2] == '\\') {
            strcpy(next_pattern, pattern + 3);
        } else {
            strcpy(next_pattern, pattern + 2);
        }
    }

#ifdef _WIN32
    WIN32_FIND_DATAA find_data;
    HANDLE hFind;
    char search_path[PATH_MAX];

    snprintf(search_path, sizeof(search_path), "%s\\*", dir_path);

    hFind = FindFirstFileA(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        return -1;
    }

    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0)
            continue;

        char full_path[PATH_MAX];
        if (strcmp(dir_path, ".") == 0) {
            snprintf(full_path, sizeof(full_path), "%s", find_data.cFileName);
        } else {
            snprintf(full_path, sizeof(full_path), "%s\\%s", dir_path, find_data.cFileName);
        }

        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recursive_search) {
                if (next_pattern[0]) {
                    cbuild_expand_wildcard_recursive(full_path, next_pattern, files,
                                                     file_count, capacity);
                }

                cbuild_expand_wildcard_recursive(full_path, pattern, files, file_count,
                                                 capacity);
            } else if (strchr(pattern, '/') || strchr(pattern, '\\')) {
                const char* slash = strchr(pattern, '/');
                if (!slash)
                    slash = strchr(pattern, '\\');

                char subdir_pattern[PATH_MAX] = { 0 };
                strncpy(subdir_pattern, pattern, slash - pattern);
                subdir_pattern[slash - pattern] = '\0';

                if (cbuild_match_wildcard(subdir_pattern, find_data.cFileName)) {
                    cbuild_expand_wildcard_recursive(full_path, slash + 1, files,
                                                     file_count, capacity);
                }
            }
        } else if (!recursive_search &&
                   cbuild_match_wildcard(pattern, find_data.cFileName)) {
            if (cbuild_add_to_file_list(files, file_count, capacity, full_path) !=
                0) {
                FindClose(hFind);
                return -1;
            }
        }
    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);
#else
    DIR* dir;
    struct dirent* entry;

    dir = opendir(dir_path);
    if (!dir) {
        return -1;
    }

    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char full_path[PATH_MAX];
        if (strcmp(dir_path, ".") == 0) {
            snprintf(full_path, sizeof(full_path), "%s", entry->d_name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        }

        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (recursive_search) {
                if (next_pattern[0]) {
                    cbuild_expand_wildcard_recursive(full_path, next_pattern, files,
                                                     file_count, capacity);
                }

                cbuild_expand_wildcard_recursive(full_path, pattern, files, file_count,
                                                 capacity);
            } else if (strchr(pattern, '/') || strchr(pattern, '\\')) {
                const char* slash = strchr(pattern, '/');
                if (!slash)
                    slash = strchr(pattern, '\\');

                char subdir_pattern[PATH_MAX] = { 0 };
                strncpy(subdir_pattern, pattern, slash - pattern);
                subdir_pattern[slash - pattern] = '\0';

                if (cbuild_match_wildcard(subdir_pattern, entry->d_name)) {
                    cbuild_expand_wildcard_recursive(full_path, slash + 1, files,
                                                     file_count, capacity);
                }
            }
        } else if (!recursive_search &&
                   cbuild_match_wildcard(pattern, entry->d_name)) {
            if (cbuild_add_to_file_list(files, file_count, capacity, full_path) !=
                0) {
                closedir(dir);
                return -1;
            }
        }
    }

    closedir(dir);
#endif
    return 0;
}

CBUILD_INTERNAL char* cbuild__join_path(const char* a, const char* b) {
    size_t la = strlen(a), lb = strlen(b);
    int need_slash = (la > 0 && a[la - 1] != '/' && a[la - 1] != '\\');
    char* out = (char*)malloc(la + lb + 2);
    strcpy(out, a);
    if (need_slash)
        strcat(out, "/");
    strcat(out, b);
    return out;
}

CBUILD_INTERNAL int cbuild__is_library_path(const char* path) {
    if (!path) return 0;
#ifdef _WIN32
    // linkable libs on Windows are .lib (and MinGW can use .a)
    if (cbuild__has_suffix(path, ".lib")) return 1;
    if (cbuild__has_suffix(path, ".a")) return 1;
    // .dll is NOT linkable directly
    return 0;
#else
#if defined(__APPLE__)
    if (cbuild__has_suffix(path, ".dylib")) return 1;
#endif
    if (cbuild__has_suffix(path, ".a")) return 1;
    if (cbuild__has_suffix(path, ".so")) return 1;
    return 0;
#endif
}

CBUILD_INTERNAL void get_dir_from_path(const char* path, char* out_dir, size_t out_size) {
    if (!path || !out_dir || out_size == 0) return;
    const char* last_slash = strrchr(path, '/');
    const char* last_backslash = strrchr(path, '\\');
    const char* separator = last_slash > last_backslash ? last_slash : last_backslash;

    if (separator) {
        size_t dir_len = separator - path;
        if (dir_len >= out_size) dir_len = out_size - 1;
        strncpy(out_dir, path, dir_len);
        out_dir[dir_len] = '\0';
    } else {
        out_dir[0] = '.';
        out_dir[1] = '\0';
    }
}

CBUILD_INTERNAL int ensure_dir_exists(const char* path) {
    if (!path || !*path)
        return 0;
    char temp[1024];
    size_t len = strlen(path);
    if (len >= sizeof(temp)) {
        return -1;  // path too long
    }
    strcpy(temp, path);
    if (temp[len - 1] == '/' || temp[len - 1] == '\\') {
        temp[len - 1] = '\0';
    }
    for (char* p = temp + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
#ifdef _WIN32
            if (_mkdir(temp) != 0) {
                if (errno != EEXIST) {
                    *p = '/';
                    return -1;
                }
            }
#else
            if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
                *p = '/';
                return -1;
            }
#endif
            *p = '/';
        }
    }
#ifdef _WIN32
    if (_mkdir(temp) != 0) {
        if (errno != EEXIST)
            return -1;
    }
#else
    if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
#endif
    return 0;
}

/* Comparison functions for sorting to stabilize build results */
CBUILD_INTERNAL int cbuild__strcmp_wrapper(const void* a, const void* b) {
    const char* sa = *(const char**)a;
    const char* sb = *(const char**)b;
    return strcmp(sa, sb);
}

CBUILD_INTERNAL void remove_file(cbuild_context_t* ctx, const char* path) {
    (void)ctx;
    if (!path || !*path)
        return;
    remove(path);
}

/* Classify a path without following links: 1 = real directory, -1 = symlink
 * (or Windows reparse point), 0 = anything else, including a missing path. */
static int cbuild__real_dir_kind(const char* path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributes(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return 0;
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return -1;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
#else
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (S_ISLNK(st.st_mode)) return -1;
    return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

/* Returns 1 when path is the current working directory or one of its ancestors
 * (which includes the filesystem root), or when that cannot be ruled out. */
static int cbuild__dir_contains_cwd(const char* path) {
#ifdef _WIN32
    char full[MAX_PATH];
    char cwd[MAX_PATH];
    if (!_fullpath(full, path, sizeof(full)) || !_getcwd(cwd, sizeof(cwd))) return 1;
    size_t n = strlen(full);
    while (n > 0 && (full[n - 1] == '\\' || full[n - 1] == '/')) n--;
    if (_strnicmp(full, cwd, n) != 0) return 0;
    return cwd[n] == '\0' || cwd[n] == '\\' || cwd[n] == '/';
#else
    struct stat st_target;
    if (stat(path, &st_target) != 0) return 0;
    char rel[PATH_MAX] = ".";
    for (;;) {
        struct stat st_cur, st_parent;
        if (stat(rel, &st_cur) != 0) return 1;
        if (st_cur.st_dev == st_target.st_dev && st_cur.st_ino == st_target.st_ino) return 1;
        if (strlen(rel) + 4 > sizeof(rel)) return 1;
        strcat(rel, "/..");
        if (stat(rel, &st_parent) != 0) return 1;
        if (st_parent.st_dev == st_cur.st_dev && st_parent.st_ino == st_cur.st_ino) return 0;
    }
#endif
}

/* Remove a directory tree that cbuild created. Symlinked directories are left
 * alone, as is anything that would take the working directory with it. */
CBUILD_INTERNAL void cbuild__clean_dir(cbuild_context_t* ctx, const char* path) {
    if (!path || !*path)
        return;
    int kind = cbuild__real_dir_kind(path);
    if (kind == 0)
        return;
    if (kind < 0) {
        cbuild__log(ctx, CBUILD_LOG_WARNING, "cbuild: not cleaning '%s': it is a symbolic link", path);
        return;
    }
    if (cbuild__dir_contains_cwd(path)) {
        cbuild__log(ctx, CBUILD_LOG_WARNING,
                    "cbuild: not cleaning '%s': it contains the current working directory", path);
        return;
    }
    remove_dir_recursive(ctx, path);
}

/* Links are removed, never followed, so nothing outside the tree is touched. */
static void remove_dir_recursive(cbuild_context_t* ctx, const char* path) {
    if (!path || !*path)
        return;
    if (cbuild__real_dir_kind(path) != 1)
        return;
#ifdef _WIN32
    WIN32_FIND_DATA ffd;
    char* pattern = cbuild__join_path(path, "*");
    HANDLE hFind = FindFirstFile(pattern, &ffd);
    free(pattern);
    if (hFind == INVALID_HANDLE_VALUE)
        return;
    do {
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0)
            continue;
        char* full = cbuild__join_path(path, ffd.cFileName);
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                _rmdir(full);
            } else {
                remove_dir_recursive(ctx, full);
            }
        } else {
            remove_file(ctx, full);
        }
        free(full);
    } while (FindNextFile(hFind, &ffd));
    FindClose(hFind);
    _rmdir(path);
#else
    DIR* dir = opendir(path);
    if (!dir)
        return;
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        char* full = cbuild__join_path(path, entry->d_name);
        struct stat st;
        if (!lstat(full, &st)) {
            if (S_ISDIR(st.st_mode)) {
                remove_dir_recursive(ctx, full);
            } else {
                remove_file(ctx, full);
            }
        }
        free(full);
    }
    closedir(dir);
    rmdir(path);
#endif
}
