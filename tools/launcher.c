#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

#ifdef _WIN32
#include <wchar.h>
#include <wctype.h>

#define BUILD_PROGRAM "cbuild.exe"
#define BUILD_PROGRAM_PATH ".\\cbuild.exe"
#define PATH_LIST_SEPARATOR ';'
#define DIR_SEPARATOR "\\"
#define USER_ENVIRONMENT_KEY "Environment"
#define ENVIRONMENT_KEY_OVERRIDE "CBUILD_LAUNCHER_ENV_KEY"
#ifdef _MSC_VER
#pragma comment(lib, "advapi32")
#pragma comment(lib, "user32")
#endif
#else
#define BUILD_PROGRAM "cbuild"
#define BUILD_PROGRAM_PATH "./cbuild"
#define PATH_LIST_SEPARATOR ':'
#define DIR_SEPARATOR "/"
#endif

#ifdef __FreeBSD__
#define INSTALL_PARENT ""
#define INSTALL_LABEL "~/bin"
#else
#define INSTALL_PARENT DIR_SEPARATOR ".local"
#define INSTALL_LABEL "~/.local/bin"
#endif

#define BUILD_SOURCE "build.c"
#define BUILD_PROGRAM_MARKER "CBUILD_IMPLEMENTATION"

static void print_usage(void) {
    printf("usage: cbuild [ARGS...]\n\n"
           "Runs the build program of the project around the working directory with ARGS,\n"
           "from the project's root. A project is the nearest directory at or above the\n"
           "working directory whose " BUILD_SOURCE " contains " BUILD_PROGRAM_MARKER ".\n"
           "When " BUILD_PROGRAM " does not exist there, it is compiled from " BUILD_SOURCE " first.\n\n"
           "Launcher options:\n"
           "  --launcher-help     Show this help and exit\n"
           "  --launcher-version  Show the launcher's version and exit\n"
           "  --launcher-install  Copy this program to " INSTALL_LABEL " and exit\n");
}

static int is_build_program(const char *path) {
    if (!cbuild_file_exists(path)) return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    int found = 0;
    long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    char *text = size >= 0 ? (char *)malloc((size_t)size + 1) : NULL;
    if (text) {
        rewind(file);
        text[fread(text, 1, (size_t)size, file)] = '\0';
        found = strstr(text, BUILD_PROGRAM_MARKER) != NULL;
        free(text);
    }
    fclose(file);
    return found;
}

/* Returns 0 with the project root as the working directory, or -1 when there is no project. */
static int enter_project(void) {
    char here[PATH_MAX];
    char parent[PATH_MAX];
    if (cbuild_get_cwd(here, sizeof(here)) != 0) return -1;
    while (!is_build_program(BUILD_SOURCE)) {
        if (chdir("..") != 0 || cbuild_get_cwd(parent, sizeof(parent)) != 0) return -1;
        if (strcmp(here, parent) == 0) return -1;
        strcpy(here, parent);
    }
    return 0;
}

static int compile_build_program(void) {
    const char *compiler = cbuild__self_rebuild_compiler();
    fprintf(stderr, "cbuild: compiling " BUILD_SOURCE " with %s\n", compiler);
    fflush(NULL);
    char *command = cbuild__self_rebuild_command(compiler, BUILD_PROGRAM, BUILD_SOURCE);
    int rc = command ? system(command) : -1;
    free(command);
    if (rc == 0) return 0;
    fprintf(stderr, "cbuild: could not compile " BUILD_SOURCE " with %s\n", compiler);
    fprintf(stderr, "hint: the project must provide the cbuild.h that " BUILD_SOURCE " includes\n");
    return -1;
}

/* Returns the program's exit status where it cannot replace this process, or 127 when it did not start. */
static int start_build_program(int argc, char **argv) {
    fflush(NULL);
#ifdef _WIN32
    cbuild_context_t *ctx = cbuild_context_new();
    const char **args = (const char **)malloc(sizeof(char *) * (size_t)argc);
    int status = -1;
    if (ctx && args) {
        args[0] = BUILD_PROGRAM_PATH;
        for (int i = 1; i < argc; ++i) args[i] = argv[i];
        status = cbuild_spawn(ctx, args, argc, NULL);
    }
    free(args);
    if (ctx) cbuild_context_free(ctx);
    if (status != -1) return status;
#else
    (void)argc;
    argv[0] = (char *)BUILD_PROGRAM_PATH;
    execv(BUILD_PROGRAM_PATH, argv);
#endif
    fprintf(stderr, "cbuild: could not start " BUILD_PROGRAM_PATH "; delete it to compile it again\n");
    return 127;
}

/* out holds PATH_MAX bytes. */
static int join_path(char *out, const char *first, const char *second) {
    int length = snprintf(out, PATH_MAX, "%s%s", first, second);
    return length < 0 || length >= PATH_MAX ? -1 : 0;
}

static const char *home_directory(void) {
    const char *home = getenv("HOME");
    if (home && *home) return home;
#ifdef _WIN32
    home = getenv("USERPROFILE");
    if (home && *home) return home;
#endif
    return NULL;
}

static int ensure_directory(const char *path) {
    if (cbuild_dir_exists(path)) return 0;
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

static int copy_program(const char *from, const char *to) {
    FILE *in = fopen(from, "rb");
    if (!in) return -1;
    FILE *out = fopen(to, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    char buffer[8192];
    size_t got;
    int rc = 0;
    while (rc == 0 && (got = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, got, out) != got) rc = -1;
    }
    if (ferror(in)) rc = -1;
    fclose(in);
    if (fclose(out) != 0) rc = -1;
#ifndef _WIN32
    if (rc == 0) rc = chmod(to, 0755);
#endif
    return rc;
}

static int replace_program(const char *replacement, const char *path) {
#ifdef _WIN32
    char old[PATH_MAX];
    if (join_path(old, path, ".old") != 0) return -1;
    remove(old);
    int moved = rename(path, old) == 0;
    if (rename(replacement, path) != 0) {
        if (moved) rename(old, path);
        return -1;
    }
    remove(old);
    return 0;
#else
    return rename(replacement, path);
#endif
}

static int same_path(const char *a, const char *b, size_t length) {
#ifdef _WIN32
    for (size_t i = 0; i < length; ++i) {
        char left = a[i] == '/' ? '\\' : (char)tolower((unsigned char)a[i]);
        char right = b[i] == '/' ? '\\' : (char)tolower((unsigned char)b[i]);
        if (left != right) return 0;
    }
    return 1;
#else
    return strncmp(a, b, length) == 0;
#endif
}

static int is_on_path(const char *directory) {
    const char *at = getenv("PATH");
    if (!at) return 0;
    size_t length = strlen(directory);
    for (;;) {
        const char *end = strchr(at, PATH_LIST_SEPARATOR);
        size_t entry = end ? (size_t)(end - at) : strlen(at);
        while (entry > 1 && (at[entry - 1] == '/' || at[entry - 1] == '\\')) entry--;
        if (entry == length && same_path(at, directory, length)) return 1;
        if (!end) return 0;
        at = end + 1;
    }
}

#ifdef _WIN32
enum { USER_PATH_FAILED = -1, USER_PATH_HAS_IT = 0, USER_PATH_ADDED = 1 };

/* Returns a malloc'd copy of text, or NULL. */
static wchar_t *to_wide(const char *text) {
    int count = MultiByteToWideChar(CP_ACP, 0, text, -1, NULL, 0);
    wchar_t *wide = count > 0 ? (wchar_t *)malloc(sizeof(wchar_t) * (size_t)count) : NULL;
    if (wide && MultiByteToWideChar(CP_ACP, 0, text, -1, wide, count) == 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

static wchar_t fold_path_char(wchar_t c) {
    return c == L'/' ? L'\\' : (wchar_t)towlower(c);
}

static int wide_list_has(const wchar_t *list, const wchar_t *directory) {
    size_t length = wcslen(directory);
    for (;;) {
        const wchar_t *end = wcschr(list, L';');
        size_t entry = end ? (size_t)(end - list) : wcslen(list);
        while (entry > 1 && (list[entry - 1] == L'/' || list[entry - 1] == L'\\')) entry--;
        if (entry == length) {
            size_t i = 0;
            while (i < length && fold_path_char(list[i]) == fold_path_char(directory[i])) i++;
            if (i == length) return 1;
        }
        if (!end) return 0;
        list = end + 1;
    }
}

/* value is the unexpanded PATH list. */
static int user_path_has(const wchar_t *value, const wchar_t *directory) {
    DWORD count = ExpandEnvironmentStringsW(value, NULL, 0);
    wchar_t *expanded = count ? (wchar_t *)calloc(count, sizeof(wchar_t)) : NULL;
    int ok = expanded && ExpandEnvironmentStringsW(value, expanded, count) != 0;
    int found = wide_list_has(ok ? expanded : value, directory);
    free(expanded);
    return found;
}

static int update_path_value(HKEY key, const wchar_t *directory) {
    DWORD type = REG_EXPAND_SZ;
    DWORD size = 0;
    LONG rc = RegQueryValueExW(key, L"Path", NULL, &type, NULL, &size);
    if (rc == ERROR_FILE_NOT_FOUND) {
        type = REG_EXPAND_SZ;
        size = 0;
    } else if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return USER_PATH_FAILED;
    }

    size_t directory_length = wcslen(directory);
    wchar_t *value = (wchar_t *)calloc(size / sizeof(wchar_t) + directory_length + 3, sizeof(wchar_t));
    if (!value) return USER_PATH_FAILED;
    DWORD read_size = size;
    if (size > 0 && RegQueryValueExW(key, L"Path", NULL, &type, (BYTE *)value, &read_size) != ERROR_SUCCESS) {
        free(value);
        return USER_PATH_FAILED;
    }
    if (user_path_has(value, directory)) {
        free(value);
        return USER_PATH_HAS_IT;
    }

    size_t length = wcslen(value);
    if (length > 0 && value[length - 1] != L';') value[length++] = L';';
    memcpy(value + length, directory, sizeof(wchar_t) * (directory_length + 1));
    DWORD bytes = (DWORD)(sizeof(wchar_t) * (length + directory_length + 1));
    rc = RegSetValueExW(key, L"Path", 0, type, (const BYTE *)value, bytes);
    free(value);
    return rc == ERROR_SUCCESS ? USER_PATH_ADDED : USER_PATH_FAILED;
}

static int add_to_user_path(const char *directory) {
    const char *override = getenv(ENVIRONMENT_KEY_OVERRIDE);
    int redirected = override && *override;
    wchar_t *key_name = to_wide(redirected ? override : USER_ENVIRONMENT_KEY);
    wchar_t *wide_directory = to_wide(directory);
    int result = USER_PATH_FAILED;
    HKEY key;
    if (key_name && wide_directory &&
        RegCreateKeyExW(HKEY_CURRENT_USER, key_name, 0, NULL, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, NULL) ==
            ERROR_SUCCESS) {
        for (wchar_t *at = wide_directory; *at; ++at) {
            if (*at == L'/') *at = L'\\';
        }
        result = update_path_value(key, wide_directory);
        RegCloseKey(key);
    }
    free(key_name);
    free(wide_directory);
    if (result == USER_PATH_ADDED && !redirected) {
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
    }
    return result;
}
#endif

static void report_path(const char *directory) {
    if (is_on_path(directory)) return;
#ifdef _WIN32
    int result = add_to_user_path(directory);
    if (result != USER_PATH_FAILED) {
        const char *state = result == USER_PATH_ADDED ? "Added to your user PATH" : "Already on your user PATH";
        printf("%s: %s; open a new terminal to use cbuild\n", state, directory);
        return;
    }
#endif
    printf("%s is not on PATH; add it to run cbuild from anywhere\n", directory);
}

static int install_launcher(const char *argv0) {
    const char *home = home_directory();
    if (!home) {
        fprintf(stderr, "cbuild: could not install: HOME is not set\n");
        return 1;
    }
    char self[PATH_MAX];
    char parent[PATH_MAX];
    char bin[PATH_MAX];
    char installed[PATH_MAX];
    char replacement[PATH_MAX];
    cbuild__self_path(self, sizeof(self), argv0);
    if (join_path(parent, home, INSTALL_PARENT) != 0 || join_path(bin, parent, DIR_SEPARATOR "bin") != 0 ||
        join_path(installed, bin, DIR_SEPARATOR BUILD_PROGRAM) != 0 || join_path(replacement, installed, ".tmp") != 0) {
        fprintf(stderr, "cbuild: could not install: the path under %s is too long\n", home);
        return 1;
    }
    if (ensure_directory(parent) != 0 || ensure_directory(bin) != 0) {
        fprintf(stderr, "cbuild: could not create %s\n", bin);
        return 1;
    }
    if (copy_program(self, replacement) != 0 || replace_program(replacement, installed) != 0) {
        remove(replacement);
        fprintf(stderr, "cbuild: could not install %s\n", installed);
        return 1;
    }
    printf("Installed %s\n", installed);
    report_path(bin);
    return 0;
}

int main(int argc, char **argv) {
    const char *first = argc > 1 ? argv[1] : "";
    if (strcmp(first, "--launcher-help") == 0) {
        print_usage();
        return 0;
    }
    if (strcmp(first, "--launcher-version") == 0) {
        printf("%s\n", CBUILD_VERSION);
        return 0;
    }
    if (strcmp(first, "--launcher-install") == 0) return install_launcher(argv[0]);

    char start[PATH_MAX];
    if (cbuild_get_cwd(start, sizeof(start)) != 0) snprintf(start, sizeof(start), "the working directory");
    if (enter_project() != 0) {
        if (strcmp(first, "--help") == 0 || strcmp(first, "-h") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(first, "--version") == 0) {
            printf("%s\n", CBUILD_VERSION);
            return 0;
        }
        fprintf(stderr, "cbuild: no " BUILD_SOURCE " containing " BUILD_PROGRAM_MARKER " in %s or its parents\n", start);
        return 1;
    }
    if (cbuild_dir_exists(BUILD_PROGRAM)) {
        fprintf(stderr, "cbuild: " BUILD_PROGRAM " in the project root is a directory\n");
        return 1;
    }
    if (!cbuild_file_exists(BUILD_PROGRAM) && compile_build_program() != 0) return 1;
    return start_build_program(argc, argv);
}
