/* target.c - target creation and per-target settings. */

#include "cbuild_internal.h"

static target_t* cbuild_create_target(cbuild_context_t* ctx, const char* name,
                                      cbuild_target_type type);

int cbuild_add_cflags(cbuild_context_t* ctx, target_t* target, const char* cflags) {
    if (!ctx || !target || !cflags) {
        if (ctx) cbuild__set_error(ctx, "Invalid argument to cbuild_add_cflags");
        return -1;
    }
    char* copy = strdup(cflags);
    if (!copy) {
        cbuild__set_error(ctx, "Out of memory in cbuild_add_cflags");
        return -1;
    }

    char* p = copy;
    while (*p) {
        while (*p && (*p == ' ' || *p == '\t')) p++;
        if (!*p) break;

        char* start = p;
        int in_quote = 0;
        while (*p) {
            if (*p == '"') {
                in_quote = !in_quote;
                p++;
            } else if (!in_quote && (*p == ' ' || *p == '\t')) {
                break;
            } else {
                p++;
            }
        }

        if (p > start) {
            char saved = *p;
            *p = '\0';

            char* token = start;
            size_t len = strlen(token);
            if (len >= 2 && token[0] == '"' && token[len - 1] == '"') {
                token[len - 1] = '\0';
                token++;
            }

            if (ensure_capacity_charpp(ctx, &target->cflags, &target->cflag_count, &target->cflag_cap) != 0) {
                free(copy);
                return -1;
            }
            target->cflags[target->cflag_count++] = strdup(token);

            *p = saved;
        }
    }

    free(copy);
    return 0;
}

int cbuild_add_ldflags(cbuild_context_t* ctx, target_t* target, const char* ldflags) {
    if (!ctx || !target || !ldflags) {
        if (ctx) cbuild__set_error(ctx, "Invalid argument to cbuild_add_ldflags");
        return -1;
    }
    char* copy = strdup(ldflags);
    if (!copy) {
        cbuild__set_error(ctx, "Out of memory in cbuild_add_ldflags");
        return -1;
    }

    char* p = copy;
    while (*p) {
        while (*p && (*p == ' ' || *p == '\t')) p++;
        if (!*p) break;

        char* start = p;
        int in_quote = 0;
        while (*p) {
            if (*p == '"') {
                in_quote = !in_quote;
                p++;
            } else if (!in_quote && (*p == ' ' || *p == '\t')) {
                break;
            } else {
                p++;
            }
        }

        if (p > start) {
            char saved = *p;
            *p = '\0';

            char* token = start;
            size_t len = strlen(token);
            if (len >= 2 && token[0] == '"' && token[len - 1] == '"') {
                token[len - 1] = '\0';
                token++;
            }

            if (ensure_capacity_charpp(ctx, &target->ldflags, &target->ldflag_count, &target->ldflag_cap) != 0) {
                free(copy);
                return -1;
            }
            target->ldflags[target->ldflag_count++] = strdup(token);

            *p = saved;
        }
    }

    free(copy);
    return 0;
}

target_t* cbuild_executable(cbuild_context_t* ctx, const char* name) {
    return cbuild_create_target(ctx, name, TARGET_EXECUTABLE);
}

target_t* cbuild_static_library(cbuild_context_t* ctx, const char* name) {
    return cbuild_create_target(ctx, name, TARGET_STATIC_LIB);
}

target_t* cbuild_shared_library(cbuild_context_t* ctx, const char* name) {
    return cbuild_create_target(ctx, name, TARGET_SHARED_LIB);
}

target_t* cbuild_dummy_target(cbuild_context_t* ctx, const char* name) {
    return cbuild_create_target(ctx, name, TARGET_DUMMY);
}

target_t* cbuild_file_dep_target(cbuild_context_t* ctx, const char* name, const char* file_path) {
    target_t* t = cbuild_create_target(ctx, name, TARGET_FILE_DEP);
    // repurpose output_file to mean required file
    if (t && file_path)
        t->output_file = strdup(file_path);
    return t;
}

int cbuild_add_source(cbuild_context_t* ctx, target_t* target, const char* source_file) {
    if (!ctx || !target || !source_file) {
        if (ctx) cbuild__set_error(ctx, "Invalid argument to cbuild_add_source");
        return -1;
    }
    if (strchr(source_file, '*') || strchr(source_file, '?')) {
        char** expanded_files = NULL;
        int file_count = 0;

        if (cbuild_expand_wildcard(source_file, &expanded_files, &file_count) ==
                0 &&
            file_count > 0) {
            for (int i = 0; i < file_count; i++) {
                if (ensure_capacity_charpp(ctx, &target->sources, &target->sources_count,
                                       &target->sources_cap) != 0) {
                    for (int j = i; j < file_count; j++) free(expanded_files[j]);
                    free(expanded_files);
                    return -1;
                }
                target->sources[target->sources_count++] =
                    expanded_files[i];
            }
            free(expanded_files);
        }
    } else {
        if (ensure_capacity_charpp(ctx, &target->sources, &target->sources_count,
                               &target->sources_cap) != 0) {
            return -1;
        }
        /* Normalize path separators for consistent hashing on Windows */
        target->sources[target->sources_count++] = cbuild__normalize_path(source_file);
    }
    return 0;
}

int cbuild_add_include_dir(cbuild_context_t* ctx, target_t* target, const char* include_dir) {
    if (!ctx || !target || !include_dir) {
        if (ctx) cbuild__set_error(ctx, "Invalid argument to cbuild_add_include_dir");
        return -1;
    }
    if (strchr(include_dir, '*') || strchr(include_dir, '?')) {
        char** expanded_dirs = NULL;
        int dir_count = 0;

        if (cbuild_expand_wildcard(include_dir, &expanded_dirs, &dir_count) == 0 &&
            dir_count > 0) {
            for (int i = 0; i < dir_count; i++) {
                if (cbuild_dir_exists(expanded_dirs[i])) {
                    if (ensure_capacity_charpp(ctx, &target->include_dirs, &target->include_count,
                                           &target->include_cap) != 0) {
                        for (int j = i; j < dir_count; j++) free(expanded_dirs[j]);
                        free(expanded_dirs);
                        return -1;
                    }
                    target->include_dirs[target->include_count++] =
                        expanded_dirs[i];
                } else {
                    free(expanded_dirs[i]);
                }
            }
            free(expanded_dirs);
        } else {
            cbuild__log(ctx, CBUILD_LOG_WARNING, "No directories found matching pattern '%s'", include_dir);
        }
    } else {
        if (ensure_capacity_charpp(ctx, &target->include_dirs, &target->include_count,
                               &target->include_cap) != 0) {
            return -1;
        }
        /* Normalize path separators for consistent signature comparison on Windows */
        target->include_dirs[target->include_count++] = cbuild__normalize_path(include_dir);
    }
    return 0;
}

int cbuild_add_library_dir(cbuild_context_t* ctx, target_t* target, const char* lib_dir) {
    if (!ctx || !target || !lib_dir) {
        if (ctx) cbuild__set_error(ctx, "Invalid argument to cbuild_add_library_dir");
        return -1;
    }
    if (strchr(lib_dir, '*') || strchr(lib_dir, '?')) {
        char** expanded_dirs = NULL;
        int dir_count = 0;

        if (cbuild_expand_wildcard(lib_dir, &expanded_dirs, &dir_count) == 0 &&
            dir_count > 0) {
            for (int i = 0; i < dir_count; i++) {
                if (cbuild_dir_exists(expanded_dirs[i])) {
                    if (ensure_capacity_charpp(ctx, &target->lib_dirs, &target->lib_dir_count,
                                           &target->lib_dir_cap) != 0) {
                        for (int j = i; j < dir_count; j++) free(expanded_dirs[j]);
                        free(expanded_dirs);
                        return -1;
                    }
                    target->lib_dirs[target->lib_dir_count++] =
                        expanded_dirs[i];
                } else {
                    free(expanded_dirs[i]);
                }
            }
            free(expanded_dirs);
        } else {
            cbuild__log(ctx, CBUILD_LOG_WARNING, "No directories found matching pattern '%s'", lib_dir);
        }
    } else {
        if (ensure_capacity_charpp(ctx, &target->lib_dirs, &target->lib_dir_count,
                               &target->lib_dir_cap) != 0) {
            return -1;
        }
        /* Normalize path separators for consistent signature comparison on Windows */
        target->lib_dirs[target->lib_dir_count++] = cbuild__normalize_path(lib_dir);
    }
    return 0;
}

int cbuild_add_link_library(cbuild_context_t* ctx, target_t* target, const char* lib) {
    if (!ctx || !target || !lib) {
        if (ctx) cbuild__set_error(ctx, "Invalid argument to cbuild_add_link_library");
        return -1;
    }
    if (strchr(lib, '*') || strchr(lib, '?')) {
        char** expanded_files = NULL;
        int file_count = 0;

        if (cbuild_expand_wildcard(lib, &expanded_files, &file_count) == 0 &&
            file_count > 0) {
            for (int i = 0; i < file_count; i++) {
                if (ensure_capacity_charpp(ctx, &target->link_libs, &target->link_lib_count,
                                       &target->link_lib_cap) != 0) {
                    for (int j = i; j < file_count; j++) free(expanded_files[j]);
                    free(expanded_files);
                    return -1;
                }
                target->link_libs[target->link_lib_count++] =
                    expanded_files[i];
            }
            free(expanded_files);
        } else {
            cbuild__log(ctx, CBUILD_LOG_WARNING, "No libraries found matching pattern '%s'", lib);
        }
    } else {
        if (ensure_capacity_charpp(ctx, &target->link_libs, &target->link_lib_count,
                               &target->link_lib_cap) != 0) {
            return -1;
        }
        /* Normalize path separators for consistent signature comparison on Windows */
        target->link_libs[target->link_lib_count++] = cbuild__normalize_path(lib);
    }
    return 0;
}

int cbuild_add_expose_library(cbuild_context_t* ctx, target_t* target, const char* lib_path) {
    if (!ctx || !target || !lib_path) {
        if (ctx) cbuild__set_error(ctx, "Invalid argument to cbuild_add_expose_library");
        return -1;
    }

    /* Tokenize once and store tokens directly */
    char* copy = strdup(lib_path);
    if (!copy) {
        cbuild__set_error(ctx, "Out of memory in cbuild_add_expose_library");
        return -1;
    }

    char* p = copy;
    while (*p) {
        while (*p && (*p == ' ' || *p == '\t')) p++;
        if (!*p) break;

        char* start = p;
        int in_quote = 0;

        while (*p) {
            if (*p == '"') {
                in_quote = !in_quote;
                p++;
            } else if (!in_quote && (*p == ' ' || *p == '\t')) {
                break;
            } else {
                p++;
            }
        }

        if (p > start) {
            char saved = *p;
            *p = '\0';

            char* token = start;
            size_t len = strlen(token);
            if (len >= 2 && token[0] == '"' && token[len - 1] == '"') {
                token[len - 1] = '\0';
                token++;
            }

            if (ensure_capacity_charpp(ctx, &target->exposed_libs, &target->exposed_lib_count,
                                   &target->exposed_lib_cap) != 0) {
                free(copy);
                return -1;
            }
            /* Normalize path separators for consistent signature comparison on Windows */
            target->exposed_libs[target->exposed_lib_count++] = cbuild__normalize_path(token);

            *p = saved;
        }
    }

    free(copy);
    return 0;
}

int cbuild_export_symbols(cbuild_context_t* ctx, target_t* target) {
    if (!ctx || !target) {
        if (ctx) cbuild__set_error(ctx, "Invalid argument to cbuild_export_symbols");
        return -1;
    }

#if defined(__linux__) || (defined(__unix__) && !defined(__APPLE__))
    return cbuild_add_ldflags(ctx, target, "-Wl,-E");
#elif defined(__APPLE__)
    return 0;
#elif defined(_WIN32)
    cbuild__log(ctx, CBUILD_LOG_WARNING, "cbuild_export_symbols() has limited effect on Windows. For MSVC, use /EXPORT:symbol or a .def file. For MinGW, consider cbuild_add_ldflags(ctx, target, \"-Wl,--export-all-symbols\").");
    return 0;
#else
    return 0;
#endif
}

int cbuild_add_link_target(cbuild_context_t* ctx, target_t* dependant, target_t* dependency) {
    if (!ctx || !dependant || !dependency) {
        if (ctx) cbuild__set_error(ctx, "Invalid argument to cbuild_add_link_target");
        return -1;
    }

    if (ensure_capacity_charpp(ctx, (char***)&dependant->dependencies,
                               &dependant->dep_count, &dependant->dep_cap) != 0) {
        return -1;
    }
    dependant->dependencies[dependant->dep_count++] = dependency;
    return 0;
}

void cbuild_target_set_linker(cbuild_context_t* ctx, target_t* t, const char* linker_exe) {
    (void)ctx;
    if (!t) return;
    if (t->linker) free(t->linker);
    t->linker = linker_exe ? strdup(linker_exe) : NULL;
}

void cbuild_set_output_file(cbuild_context_t* ctx, target_t* t, const char* path) {
    (void)ctx;
    if (!t || !path) return;
    if (t->output_file) free(t->output_file);
    /* Normalize path separators for consistent path handling on Windows */
    t->output_file = cbuild__normalize_path(path);
    t->output_file_explicit = 1;
}

void cbuild_set_soname(cbuild_context_t* ctx, target_t* t, const char* soname) {
    (void)ctx;
    if (!t || !soname) return;
    if (t->soname) free(t->soname);
    t->soname = strdup(soname);
}

CBUILD_INTERNAL int cbuild__add_define_to_list(cbuild_context_t* ctx, char*** arr, int* count, int* cap,
                                                const char* macro,
                                                const char* value_optional) {
    char* entry = NULL;
    if (value_optional) {
        if (append_format(&entry, "%s=%s", macro, value_optional) != 0) {
            cbuild__set_error(ctx, "Out of memory in cbuild__add_define_to_list");
            return -1;
        }
    } else {
        entry = strdup(macro);
        if (!entry) {
            cbuild__set_error(ctx, "Out of memory in cbuild__add_define_to_list");
            return -1;
        }
    }

    if (ensure_capacity_charpp(ctx, arr, count, cap) != 0) {
        free(entry);
        return -1;
    }
    (*arr)[(*count)++] = entry;
    return 0;
}

int cbuild_add_define(cbuild_context_t* ctx, target_t* t, const char* macro) {
    if (!ctx || !t || !macro) {
        if (ctx) cbuild__set_error(ctx, "Invalid argument to cbuild_add_define");
        return -1;
    }
    return cbuild__add_define_to_list(ctx, &t->defines, &t->define_count, &t->define_cap,
                                      macro, NULL);
}

int cbuild_add_define_val(cbuild_context_t* ctx, target_t* t, const char* macro, const char* val) {
    if (!ctx || !t || !macro) {
        if (ctx) cbuild__set_error(ctx, "Invalid argument to cbuild_add_define_val");
        return -1;
    }
    return cbuild__add_define_to_list(ctx, &t->defines, &t->define_count, &t->define_cap,
                                      macro, val);
}

int cbuild_add_flag(cbuild_context_t* ctx, target_t* t, const char* flag, int value) {
    if (!ctx || !t || !flag) {
        if (ctx) cbuild__set_error(ctx, "Invalid argument to cbuild_add_flag");
        return -1;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", !!value);
    return cbuild_add_define_val(ctx, t, flag, buf);
}

static target_t* cbuild_create_target(cbuild_context_t* ctx, const char* name,
                                      cbuild_target_type type) {
    target_t* t = (target_t*)calloc(1, sizeof(target_t));
    t->type = type;
    t->name = strdup(name);
    char *out = NULL, *obj = NULL;
    const char *output_dir = ctx->active_config ? (ctx->active_config->output_dir ? ctx->active_config->output_dir : ctx->output_dir) : ctx->output_dir;
    if (type == TARGET_EXECUTABLE) {
#ifdef _WIN32
        append_format(&out, "%s/%s.exe", output_dir, name);
#else
        append_format(&out, "%s/%s", output_dir, name);
#endif
    } else if (type == TARGET_STATIC_LIB) {
#ifdef _WIN32
        append_format(&out, "%s/%s.lib", output_dir, name);
#else
        append_format(&out, "%s/lib%s.a", output_dir, name);
#endif
    } else if (type == TARGET_SHARED_LIB) {
#ifdef _WIN32
        append_format(&out, "%s/%s.dll", output_dir, name);
#elif __APPLE__
        append_format(&out, "%s/lib%s.dylib", output_dir, name);
#else
        append_format(&out, "%s/lib%s.so", output_dir, name);
#endif
    }

    append_format(&obj, "%s/obj_%s", output_dir, name);
    t->output_file = out;
    t->obj_dir = obj;
    t->commands = NULL;
    t->cmd_count = t->cmd_cap = 0;
    t->post_commands = NULL;
    t->post_cmd_count = t->post_cmd_cap = 0;
    t->compiler = NULL;
    t->linker = NULL;
    t->cc_kind_override = -1;
    ensure_capacity_charpp(ctx, (char***)&ctx->targets, &ctx->target_count, &ctx->target_cap);
    ctx->targets[ctx->target_count++] = t;
    return t;
}
