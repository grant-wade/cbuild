/* build.c - target graph traversal, clean, and the per-target build and link steps. */

#include "cbuild_internal.h"

static void build_target(cbuild_context_t* ctx, target_t* t, int* error_flag);

static void dfs_command_func(cbuild_context_t* ctx, command_t* cmd, int* error_flag_ptr) {
    if (!cmd || *error_flag_ptr)
        return;
    if (cmd->executed)
        return;
    /* cbuild_run_command runs dependencies first and rejects cycles */
    if (cbuild_run_command(ctx, cmd) != 0) {
        *error_flag_ptr = 1;
    }
}

CBUILD_INTERNAL void dfs_build_func(cbuild_context_t* ctx, target_t* t, int* error_flag_ptr) {
    int ti = -1;
    for (int j = 0; j < ctx->target_count; ++j) {
        if (ctx->targets[j] == t) {
            ti = j;
            break;
        }
    }
    if (ti == -1)
        return;
    if (*error_flag_ptr)
        return;
    if (ctx->in_stack[ti]) {
        cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: circular dependency involving %s", t->name);
        *error_flag_ptr = 1;
        return;
    }
    if (ctx->visited[ti])
        return;
    ctx->in_stack[ti] = 1;

    if (t->type == TARGET_FILE_DEP) {
        /* Its commands only run when the file is stale, so unlike other targets
         * the dependencies (e.g. a generator tool) come first. */
        for (int di = 0; di < t->dep_count; ++di) {
            dfs_build_func(ctx, t->dependencies[di], error_flag_ptr);
            if (*error_flag_ptr) {
                ctx->in_stack[ti] = 0;
                return;
            }
        }
        build_target(ctx, t, error_flag_ptr);
        ctx->visited[ti] = 1;
        ctx->in_stack[ti] = 0;
        return;
    }

    for (int ci = 0; ci < t->cmd_count; ++ci) {
        dfs_command_func(ctx, t->commands[ci], error_flag_ptr);
        if (*error_flag_ptr) {
            ctx->in_stack[ti] = 0;
            return;
        }
    }

    for (int di = 0; di < t->dep_count; ++di) {
        dfs_build_func(ctx, t->dependencies[di], error_flag_ptr);
        if (*error_flag_ptr) {
            ctx->in_stack[ti] = 0;
            return;
        }
    }
    build_target(ctx, t, error_flag_ptr);
    for (int pci = 0; pci < t->post_cmd_count; ++pci) {
        dfs_command_func(ctx, t->post_commands[pci], error_flag_ptr);
        if (*error_flag_ptr) {
            ctx->in_stack[ti] = 0;
            return;
        }
    }
    ctx->visited[ti] = 1;
    ctx->in_stack[ti] = 0;
}

/* Recalculate target output paths based on per-target or active config.
 * Called after PRE flags are processed so config changes take effect.
 * Also resets config_applied flag so the full config (flags, optimizations, etc.)
 * gets applied with the correct active config. */
CBUILD_INTERNAL void cbuild__resolve_target_paths(cbuild_context_t* ctx) {
    const char *default_output_dir = ctx->active_config ? (ctx->active_config->output_dir ? ctx->active_config->output_dir : ctx->output_dir) : ctx->output_dir;

    for (int i = 0; i < ctx->target_count; ++i) {
        target_t* t = ctx->targets[i];

        /* Every build pass starts here, so this is also where per-pass state resets. */
        t->rebuilt = 0;

        /* Skip targets whose paths must not be derived from this context. */
        if (t->external || t->type == TARGET_COMMAND || t->type == TARGET_DUMMY || t->type == TARGET_FILE_DEP) {
            continue;
        }

        /* Look up per-target config to get its output_dir if set */
        const char *output_dir = default_output_dir;
        for (int j = 0; j < ctx->cfg_count; ++j) {
            if (ctx->cfg_targets[j] == t && ctx->cfg_values[j] && ctx->cfg_values[j]->output_dir) {
                output_dir = ctx->cfg_values[j]->output_dir;
                break;
            }
        }

        /* Recalculate obj_dir */
        if (t->obj_dir) free(t->obj_dir);
        t->obj_dir = NULL;
        append_format(&t->obj_dir, "%s/obj_%s", output_dir, t->name);

        /* A path chosen with cbuild_set_output_file is not ours to re-derive. */
        if (t->output_file_explicit) continue;

        if (t->output_file) {
            free(t->output_file);
            t->output_file = NULL;
        }

        /* Recalculate output_file based on type */
        char *out = NULL;
        if (t->type == TARGET_EXECUTABLE) {
#ifdef _WIN32
            append_format(&out, "%s/%s.exe", output_dir, t->name);
#else
            append_format(&out, "%s/%s", output_dir, t->name);
#endif
        } else if (t->type == TARGET_STATIC_LIB) {
#ifdef _WIN32
            append_format(&out, "%s/%s.lib", output_dir, t->name);
#else
            append_format(&out, "%s/lib%s.a", output_dir, t->name);
#endif
        } else if (t->type == TARGET_SHARED_LIB) {
#ifdef _WIN32
            append_format(&out, "%s/%s.dll", output_dir, t->name);
#elif __APPLE__
            append_format(&out, "%s/lib%s.dylib", output_dir, t->name);
#else
            append_format(&out, "%s/lib%s.so", output_dir, t->name);
#endif
        }

        t->output_file = out;
    }
}

int cbuild_clean(cbuild_context_t* ctx) {
    cbuild_init(ctx);
    cbuild__resolve_target_paths(ctx);

    cbuild__log_step(ctx, "CLEAN", CBUILD_COLOR_YELLOW, "Cleaning build outputs...");
    for (int i = 0; i < ctx->subproject_count; ++i) {
        subproject_t* sub = ctx->subprojects[i];
        cbuild__log_step(ctx, "CLEAN", CBUILD_COLOR_YELLOW, "Cleaning subproject: %s", sub->alias);
        char old_cwd[PATH_MAX];
        if (cbuild_get_cwd(old_cwd, sizeof(old_cwd)) == 0) {
            if (chdir(sub->directory) == 0) {
                cbuild_argv_t avv;
                cbuild_argv_init(&avv);
                cbuild_argv_append(&avv, sub->cbuild_exe);
                cbuild_argv_append(&avv, "--clean");
                (void)cbuild_spawn_process(ctx, &avv, 0, NULL);
                cbuild_argv_free(&avv);
                chdir(old_cwd);
            }
        }
    }
    for (int i = 0; i < ctx->target_count; ++i) {
        target_t* t = ctx->targets[i];
        if (t->external) continue; /* cleaned by its owning subproject */
        /* A file-dep target without commands names a file cbuild did not create. */
        if (t->type == TARGET_FILE_DEP && t->cmd_count == 0) continue;
        if (t->obj_dir) cbuild__clean_dir(ctx, t->obj_dir);
        if (t->output_file) {
            remove_file(ctx, t->output_file);
            if (t->type != TARGET_FILE_DEP) {
                char* link_sig = NULL;
                if (append_format(&link_sig, "%s.link.sig", t->output_file) == 0) remove_file(ctx, link_sig);
                free(link_sig);
            }
        }
    }
    if (ctx->active_config && ctx->active_config->output_dir) {
        cbuild__clean_dir(ctx, ctx->active_config->output_dir);
    }
    cbuild__clean_dir(ctx, ctx->output_dir);
    cbuild__log_status(ctx, 1, "Clean complete.");
    return 0;
}

int cbuild_build(cbuild_context_t* ctx, const char* target_name) {
    cbuild_init(ctx);
    cbuild__resolve_target_paths(ctx);

    if (ctx->cc_entries) {
        for (int i = 0; i < ctx->cc_count; ++i) {
            free(ctx->cc_entries[i].directory);
            free(ctx->cc_entries[i].command);
            free(ctx->cc_entries[i].file);
            if (ctx->cc_entries[i].arguments) {
                for (int j = 0; j < ctx->cc_entries[i].argc; ++j) {
                    free(ctx->cc_entries[i].arguments[j]);
                }
                free(ctx->cc_entries[i].arguments);
            }
        }
        free(ctx->cc_entries);
        ctx->cc_entries = NULL;
    }
    ctx->cc_count = 0;
    ctx->cc_cap = 0;

    if (ctx->job_queue) {
        free(ctx->job_queue);
        ctx->job_queue = NULL;
        ctx->job_capacity = 0;
    }

    if (ctx->generate_compile_commands) {
        for (int i = 0; i < ctx->target_count; ++i) {
            collect_compile_commands_for_target(ctx, ctx->targets[i]);
        }
    }

    int error_flag = 0;
    if (ctx->visited) free(ctx->visited);
    if (ctx->in_stack) free(ctx->in_stack);
    ctx->visited = calloc(ctx->target_count, sizeof(int));
    ctx->in_stack = calloc(ctx->target_count, sizeof(int));

    if (target_name) {
        target_t* target_to_build = NULL;
        for (int i = 0; i < ctx->target_count; ++i) {
            if (ctx->targets[i]->name && strcmp(ctx->targets[i]->name, target_name) == 0) {
                target_to_build = ctx->targets[i];
                break;
            }
        }
        if (!target_to_build) {
            cbuild__set_error(ctx, "Target '%s' not found", target_name);
            cbuild__log(ctx, CBUILD_LOG_ERROR, "Target '%s' not found", target_name);
            free(ctx->visited);
            free(ctx->in_stack);
            ctx->visited = NULL;
            ctx->in_stack = NULL;
            return -1;
        }
        dfs_build_func(ctx, target_to_build, &error_flag);
    } else {
        int* indegree = calloc(ctx->target_count, sizeof(int));
        for (int i = 0; i < ctx->target_count; ++i) {
            target_t* t = ctx->targets[i];
            for (int d = 0; d < t->dep_count; ++d) {
                target_t* dep = t->dependencies[d];
                for (int j = 0; j < ctx->target_count; ++j) {
                    if (ctx->targets[j] == dep) {
                        indegree[j]++;
                        break;
                    }
                }
            }
        }

        for (int i = 0; i < ctx->target_count; ++i) {
            target_t* t = ctx->targets[i];
            if (t->type == TARGET_FILE_DEP && indegree[i] == 0) continue;
            if (!ctx->visited[i]) {
                dfs_build_func(ctx, t, &error_flag);
                if (error_flag) break;
            }
        }
        free(indegree);
    }

    free(ctx->visited);
    ctx->visited = NULL;
    free(ctx->in_stack);
    ctx->in_stack = NULL;

    if (!error_flag) {
        if (ctx->generate_compile_commands) {
            const char* output_dir = ctx->active_config ? (ctx->active_config->output_dir ? ctx->active_config->output_dir : ctx->output_dir) : ctx->output_dir;
            ensure_dir_exists(output_dir);
            char path[1024];
            snprintf(path, sizeof(path), "%s/compile_commands.json", output_dir);
            FILE* f = fopen(path, "w");
            if (f) {
                fprintf(f, "[\n");
                for (int i = 0; i < ctx->cc_count; i++) {
                    fprintf(f, "  {\n    \"directory\": ");
                    fprint_json_string(f, ctx->cc_entries[i].directory);
                    fprintf(f, ",\n    \"arguments\": [");
                    for (int j = 0; j < ctx->cc_entries[i].argc; ++j) {
                        if (j > 0) fprintf(f, ", ");
                        fprint_json_string(f, ctx->cc_entries[i].arguments[j]);
                    }
                    fprintf(f, "],\n    \"file\": ");
                    fprint_json_string(f, ctx->cc_entries[i].file);
                    fprintf(f, "\n  }%s\n", (i + 1 < ctx->cc_count) ? "," : "");
                }
                fprintf(f, "]\n");
                fclose(f);
            }
        }
        cbuild__log_status(ctx, 1, "Build succeeded.");
        return 0;
    } else {
        cbuild__log_status(ctx, 0, "Build failed.");
        return -1;
    }
}

static void build_target(cbuild_context_t* ctx, target_t* t, int* error_flag) {
    if (t->external) {
        if (!cbuild_file_exists(t->output_file)) {
            cbuild__log_status(ctx, 0, "Subproject output '%s' was not produced", t->output_file);
            *error_flag = 1;
        }
        return;
    }

    /* Compute and apply effective build configuration (defaults → active → target) */
    cbuild__apply_config_if_needed(ctx, t);

    /* Use target's cc_kind_override if set, otherwise fall back to context */
    cbuild_cc_kind_t cc_kind = cbuild_target_cc_kind(ctx, t);

    if (t->type == TARGET_DUMMY) {
        cbuild__log_step(ctx, "DUMMY", CBUILD_COLOR_MAGENTA, "%s", t->name);
        return;
    }

    if (t->type == TARGET_FILE_DEP) {
        int should_update = 0;
        cbuild_mtime_t out_mtime = 0;

        if (cbuild__mtime(t->output_file, &out_mtime) != 0) {
            should_update = 1;  // output missing
        } else {
            for (int i = 0; i < t->sources_count; ++i) {
                cbuild_mtime_t src_mtime;
                if (cbuild__mtime(t->sources[i], &src_mtime) == 0 &&
                    src_mtime > out_mtime) {
                    should_update = 1;  // source newer than output
                    break;
                }
            }
            for (int i = 0; i < t->dep_count && !should_update; ++i) {
                target_t* dep = t->dependencies[i];
                cbuild_mtime_t dep_mtime;
                /* dep->rebuilt catches a dependency rebuilt within the same
                 * timestamp tick as the existing output */
                if (dep->rebuilt || (dep->output_file && cbuild__mtime(dep->output_file, &dep_mtime) == 0 &&
                                     dep_mtime > out_mtime)) {
                    should_update = 1;  // dependency output newer than output
                }
            }
        }

        if (should_update) {
            for (int i = 0; i < t->cmd_count; ++i) {
                if (cbuild_run_command(ctx, t->commands[i]) != 0) {
                    *error_flag = 1;
                    return;
                }
            }
            if (!cbuild_file_exists(t->output_file)) {
                cbuild__log_status(ctx, 0, "Required file '%s' missing after commands in target '%s'", t->output_file, t->name);
                *error_flag = 1;
                return;
            }
            cbuild__log_step(ctx, "FILE_DEP", CBUILD_COLOR_GREEN, "%s (updated)", t->output_file);
            t->rebuilt = 1;
        }
        return;
    }

    int compiled_sources = process_compile_jobs_parallel(ctx, t, error_flag);
    if (*error_flag) return;

    int obj_count = t->sources_count;
    char** obj_files = (char**)calloc(obj_count, sizeof(char*));
    for (int i = 0; i < obj_count; ++i) {
        const char* src = t->sources[i];
        const char* slash = strrchr(src, '/');
        const char* bslash = strrchr(src, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
        const char* base = slash ? slash + 1 : src;
        char* dot = strrchr(base, '.');
        size_t len = dot ? (size_t)(dot - base) : strlen(base);
        unsigned h = cbuild__hash_path(src);
        char objname[512];
        snprintf(objname, sizeof(objname), "%s/%.*s-%08x" CBUILD_OBJ_EXT, t->obj_dir, (int)len, base, h);
        obj_files[i] = strdup(objname);
    }

    int needs_link = compiled_sources > 0;
    cbuild_mtime_t out_mtime;
    if (cbuild__mtime(t->output_file, &out_mtime) != 0) {
        needs_link = 1;
    } else {
        for (int i = 0; i < obj_count; ++i) {
            cbuild_mtime_t obj_mtime;
            if (cbuild__mtime(obj_files[i], &obj_mtime) != 0 ||
                obj_mtime > out_mtime) {
                needs_link = 1;
                break;
            }
        }
        if (!needs_link) {
            for (int i = 0; i < t->dep_count; ++i) {
                target_t* dep = t->dependencies[i];
                /* A dependency rebuilt in this pass may share a timestamp tick
                 * with the existing output, so do not rely on mtime alone. */
                if (dep->rebuilt) {
                    needs_link = 1;
                    break;
                }
                if (dep->output_file) {
                    cbuild_mtime_t dep_mtime;
                    if (cbuild__mtime(dep->output_file, &dep_mtime) == 0 && dep_mtime > out_mtime) {
                        needs_link = 1;
                        break;
                    }
                }
            }
        }
    }

    /* If timestamps say "up-to-date", also verify the link command signature */
    if (!needs_link) {
        char* link_sig = NULL;
        cbuild_argv_t largv;
        cbuild_argv_init(&largv);

        const char* ld = cbuild_target_linker(ctx, t);
        if (t->type == TARGET_STATIC_LIB) {
            cbuild_argv_append(&largv, ctx->ar);
            if (detect_ar_kind(ctx->ar) == CBUILD_CC_MSVC) {
                char out_arg[1024];
                snprintf(out_arg, sizeof(out_arg), "/OUT:%s", t->output_file);
                cbuild_argv_append(&largv, out_arg);
            } else {
                cbuild_argv_append(&largv, "rcs");
                cbuild_argv_append(&largv, t->output_file);
            }
            char** tmp_objs = (char**)malloc(sizeof(char*) * obj_count);
            for (int i = 0; i < obj_count; ++i) tmp_objs[i] = obj_files[i];
            qsort(tmp_objs, obj_count, sizeof(char*), cbuild__strcmp_wrapper);
            for (int i = 0; i < obj_count; ++i) cbuild_argv_append(&largv, tmp_objs[i]);
            free(tmp_objs);
        } else if (t->type == TARGET_EXECUTABLE || t->type == TARGET_SHARED_LIB) {
            cbuild_argv_append(&largv, ld);
            if (cc_kind == CBUILD_CC_MSVC) {
                cbuild_argv_append(&largv, "/nologo");
                char fe_arg[1024];
                snprintf(fe_arg, sizeof(fe_arg), "/Fe:%s", t->output_file);
                cbuild_argv_append(&largv, fe_arg);
            } else {
                cbuild_argv_append(&largv, "-o");
                cbuild_argv_append(&largv, t->output_file);
            }

            char** tmp_objs = (char**)malloc(sizeof(char*) * obj_count);
            for (int i = 0; i < obj_count; ++i) tmp_objs[i] = obj_files[i];
            qsort(tmp_objs, obj_count, sizeof(char*), cbuild__strcmp_wrapper);
            for (int i = 0; i < obj_count; ++i) cbuild_argv_append(&largv, tmp_objs[i]);
            free(tmp_objs);

            if (t->lib_dir_count > 0) {
                char** tmp = (char**)malloc(sizeof(char*) * t->lib_dir_count);
                for (int i = 0; i < t->lib_dir_count; ++i) tmp[i] = t->lib_dirs[i];
                qsort(tmp, t->lib_dir_count, sizeof(char*), cbuild__strcmp_wrapper);
                for (int i = 0; i < t->lib_dir_count; ++i) {
                    char lib_arg[1024];
                    if (cc_kind == CBUILD_CC_MSVC) {
                        snprintf(lib_arg, sizeof(lib_arg), "/LIBPATH:%s", tmp[i]);
                    } else {
                        snprintf(lib_arg, sizeof(lib_arg), "-L%s", tmp[i]);
                    }
                    cbuild_argv_append(&largv, lib_arg);
                }
                free(tmp);
            }

            if (t->exposed_lib_count > 0) {
                if (cc_kind != CBUILD_CC_MSVC) {
#if defined(__linux__) || defined(__unix__) && !defined(__APPLE__)
                    cbuild_argv_append(&largv, "-Wl,-E");
#endif
                }
            }

            if (t->exposed_lib_count > 0) {
                char** tmp = (char**)malloc(sizeof(char*) * t->exposed_lib_count);
                for (int i = 0; i < t->exposed_lib_count; ++i) tmp[i] = t->exposed_libs[i];
                qsort(tmp, t->exposed_lib_count, sizeof(char*), cbuild__strcmp_wrapper);
                for (int i = 0; i < t->exposed_lib_count; ++i) {
                    cbuild_argv_append(&largv, tmp[i]);
                }
                free(tmp);
            }

            if (t->link_lib_count > 0) {
                for (int i = 0; i < t->link_lib_count; ++i) {
                    const char* lib = t->link_libs[i];
                    if (cc_kind == CBUILD_CC_MSVC) {
                        if (strchr(lib, '\\') || strchr(lib, ':')) {
                            cbuild_argv_append(&largv, lib);
                        } else {
                            char lib_arg[512];
                            snprintf(lib_arg, sizeof(lib_arg), "%s.lib", lib);
                            cbuild_argv_append(&largv, lib_arg);
                        }
                    } else {
                        if (strchr(lib, '/')) {
                            cbuild_argv_append(&largv, lib);
                        } else {
                            char lib_arg[512];
                            snprintf(lib_arg, sizeof(lib_arg), "-l%s", lib);
                            cbuild_argv_append(&largv, lib_arg);
                        }
                    }
                }
            }

            if (t->dep_count > 0) {
                for (int i = 0; i < t->dep_count; ++i) {
                    target_t* dep = t->dependencies[i];
                    if (dep->type == TARGET_STATIC_LIB) {
                        cbuild_argv_append(&largv, dep->output_file);
                    } else if (dep->type == TARGET_SHARED_LIB) {
#ifdef _WIN32
                        /* On Windows, link against .lib import library, not .dll */
                        char lib_path[1024];
                        strncpy(lib_path, dep->output_file, sizeof(lib_path) - 1);
                        lib_path[sizeof(lib_path) - 1] = '\0';
                        size_t len = strlen(lib_path);
                        if (len > 4 && strcmp(lib_path + len - 4, ".dll") == 0) {
                            strcpy(lib_path + len - 4, ".lib");
                        }
                        cbuild_argv_append(&largv, lib_path);
#else
                        cbuild_argv_append(&largv, dep->output_file);
#endif
                    } else if (dep->type == TARGET_FILE_DEP && dep->output_file &&
                               cbuild__is_library_path(dep->output_file)) {
                        cbuild_argv_append(&largv, dep->output_file);
                    }
                }
            }

            for (int i = 0; i < t->ldflag_count; ++i)
                cbuild_argv_append(&largv, t->ldflags[i]);
            for (int i = 0; i < ctx->global_ldflag_count; ++i)
                cbuild_argv_append(&largv, ctx->global_ldflags[i]);

            if (t->type == TARGET_SHARED_LIB) {
                if (cc_kind == CBUILD_CC_MSVC) {
                    cbuild_argv_append(&largv, "/LD");
                } else {
#ifdef __APPLE__
                    cbuild_argv_append(&largv, "-dynamiclib");
                    if (t->soname) {
                        char soname_arg[1024];
                        snprintf(soname_arg, sizeof(soname_arg), "-install_name");
                        cbuild_argv_append(&largv, soname_arg);
                        snprintf(soname_arg, sizeof(soname_arg), "@rpath/%s", t->soname);
                        cbuild_argv_append(&largv, soname_arg);
                    }
#else
                    cbuild_argv_append(&largv, "-shared");
                    if (t->soname) {
                        char soname_arg[1024];
                        snprintf(soname_arg, sizeof(soname_arg), "-Wl,-soname,%s", t->soname);
                        cbuild_argv_append(&largv, soname_arg);
                    }
#endif
                }
            }
        }

        for (int i = 0; i < largv.count; ++i) {
            append_format(&link_sig, "%s\n", largv.args[i]);
        }
        const char* ev = getenv("LDFLAGS");
        if (ev) append_format(&link_sig, "ENV:LDFLAGS=%s\n", ev);

        cbuild_argv_free(&largv);

        char link_sig_path[1024];
        snprintf(link_sig_path, sizeof(link_sig_path), "%s.link.sig", t->output_file);

        int link_sig_mismatch = 0;
        FILE* lf = fopen(link_sig_path, "rb");
        if (!lf) {
            link_sig_mismatch = 1;
        } else {
            fseek(lf, 0, SEEK_END);
            long lsz = ftell(lf);
            fseek(lf, 0, SEEK_SET);
            char* prev = (char*)malloc((size_t)lsz + 1);
            if (!prev) {
                link_sig_mismatch = 1;
            } else {
                size_t rd = fread(prev, 1, (size_t)lsz, lf);
                prev[rd] = '\0';
                if (strcmp(prev, link_sig ? link_sig : "") != 0) {
                    link_sig_mismatch = 1;
                }
                free(prev);
            }
            fclose(lf);
        }
        if (link_sig) free(link_sig);
        if (link_sig_mismatch) {
            needs_link = 1;
        }
    }
    if (needs_link) {
        char output_dir[1024];
        get_dir_from_path(t->output_file, output_dir, sizeof(output_dir));
        ensure_dir_exists(output_dir);

        cbuild__log_step(ctx, "LINK", CBUILD_COLOR_YELLOW, "%s", t->output_file);

        /* Build argv for linking */
        cbuild_argv_t argv;
        cbuild_argv_init(&argv);

        const char* ld = cbuild_target_linker(ctx, t);
        if (t->type == TARGET_STATIC_LIB) {
            /* Use append_flags to handle archivers with spaces (e.g., "zig ar") */
            cbuild_argv_append_flags(&argv, ctx->ar);
            if (detect_ar_kind(ctx->ar) == CBUILD_CC_MSVC) {
                char out_arg[1024];
                snprintf(out_arg, sizeof(out_arg), "/OUT:%s", t->output_file);
                cbuild_argv_append(&argv, out_arg);
            } else {
                cbuild_argv_append(&argv, "rcs");
                cbuild_argv_append(&argv, t->output_file);
            }
            /* Sort object files to stabilize build results */
            qsort(obj_files, obj_count, sizeof(char*), cbuild__strcmp_wrapper);
            for (int i = 0; i < obj_count; ++i)
                cbuild_argv_append(&argv, obj_files[i]);

        } else if (t->type == TARGET_EXECUTABLE || t->type == TARGET_SHARED_LIB) {
            /* Use append_flags to handle linkers with spaces (e.g., "zig cc -target ...") */
            cbuild_argv_append_flags(&argv, ld);

            if (cc_kind == CBUILD_CC_MSVC) {
                cbuild_argv_append(&argv, "/nologo");
                char fe_arg[1024];
                snprintf(fe_arg, sizeof(fe_arg), "/Fe:%s", t->output_file);
                cbuild_argv_append(&argv, fe_arg);
            } else {
                cbuild_argv_append(&argv, "-o");
                cbuild_argv_append(&argv, t->output_file);
            }

            /* Sort object files to stabilize build results */
            qsort(obj_files, obj_count, sizeof(char*), cbuild__strcmp_wrapper);
            for (int i = 0; i < obj_count; ++i)
                cbuild_argv_append(&argv, obj_files[i]);

            /* Sort library directories to stabilize build results */
            if (t->lib_dir_count > 0)
                qsort(t->lib_dirs, t->lib_dir_count, sizeof(char*), cbuild__strcmp_wrapper);
            for (int i = 0; i < t->lib_dir_count; ++i) {
                char lib_arg[1024];
                if (cc_kind == CBUILD_CC_MSVC) {
                    snprintf(lib_arg, sizeof(lib_arg), "/LIBPATH:%s", t->lib_dirs[i]);
                } else {
                    snprintf(lib_arg, sizeof(lib_arg), "-L%s", t->lib_dirs[i]);
                }
                cbuild_argv_append(&argv, lib_arg);
            }

            if (t->exposed_lib_count > 0) {
                // Export dynamic symbols for plugin/dlopen use
                if (cc_kind != CBUILD_CC_MSVC) {
#if defined(__linux__) || defined(__unix__) && !defined(__APPLE__)
                    cbuild_argv_append(&argv, "-Wl,-E");
#endif
                }
            }

            /* Sort exposed libraries to stabilize build results */
            if (t->exposed_lib_count > 0)
                qsort(t->exposed_libs, t->exposed_lib_count, sizeof(char*), cbuild__strcmp_wrapper);
            for (int i = 0; i < t->exposed_lib_count; ++i) {
                cbuild_argv_append(&argv, t->exposed_libs[i]);
            }

            for (int i = 0; i < t->link_lib_count; ++i) {
                if (cc_kind == CBUILD_CC_MSVC) {
                    /* Skip 'm' (math) library on MSVC - it's part of the CRT */
                    if (strcmp(t->link_libs[i], "m") == 0) continue;
                    if (strchr(t->link_libs[i], '\\') || strchr(t->link_libs[i], ':')) {
                        cbuild_argv_append(&argv, t->link_libs[i]);
                    } else {
                        char lib_arg[512];
                        snprintf(lib_arg, sizeof(lib_arg), "%s.lib", t->link_libs[i]);
                        cbuild_argv_append(&argv, lib_arg);
                    }
                } else {
                    if (strchr(t->link_libs[i], '/')) {
                        cbuild_argv_append(&argv, t->link_libs[i]);
                    } else {
                        char lib_arg[512];
                        snprintf(lib_arg, sizeof(lib_arg), "-l%s", t->link_libs[i]);
                        cbuild_argv_append(&argv, lib_arg);
                    }
                }
            }

            for (int i = 0; i < t->dep_count; ++i) {
                target_t* dep = t->dependencies[i];
                if (dep->type == TARGET_STATIC_LIB) {
                    cbuild_argv_append(&argv, dep->output_file);
                } else if (dep->type == TARGET_SHARED_LIB) {
#ifdef _WIN32
                    /* On Windows, link against .lib import library, not .dll */
                    char lib_path[1024];
                    strncpy(lib_path, dep->output_file, sizeof(lib_path) - 1);
                    lib_path[sizeof(lib_path) - 1] = '\0';
                    size_t len = strlen(lib_path);
                    if (len > 4 && strcmp(lib_path + len - 4, ".dll") == 0) {
                        strcpy(lib_path + len - 4, ".lib");
                    }
                    cbuild_argv_append(&argv, lib_path);
#else
                    cbuild_argv_append(&argv, dep->output_file);
#endif
                }
                // FILE_DEP is linkable iff its output looks like a library path.
                else if (dep->type == TARGET_FILE_DEP && dep->output_file &&
                         cbuild__is_library_path(dep->output_file)) {
                    cbuild_argv_append(&argv, dep->output_file);
                }
            }

            for (int i = 0; i < t->ldflag_count; ++i)
                cbuild_argv_append(&argv, t->ldflags[i]);
            for (int i = 0; i < ctx->global_ldflag_count; ++i)
                cbuild_argv_append(&argv, ctx->global_ldflags[i]);

            if (t->type == TARGET_SHARED_LIB) {
                if (cc_kind == CBUILD_CC_MSVC) {
                    cbuild_argv_append(&argv, "/LD");
                } else {
#ifdef __APPLE__
                    cbuild_argv_append(&argv, "-dynamiclib");
                    if (t->soname) {
                        char soname_arg[1024];
                        snprintf(soname_arg, sizeof(soname_arg), "-install_name");
                        cbuild_argv_append(&argv, soname_arg);
                        snprintf(soname_arg, sizeof(soname_arg), "@rpath/%s", t->soname);
                        cbuild_argv_append(&argv, soname_arg);
                    }
#else
                    cbuild_argv_append(&argv, "-shared");
                    if (t->soname) {
                        char soname_arg[1024];
                        snprintf(soname_arg, sizeof(soname_arg), "-Wl,-soname,%s", t->soname);
                        cbuild_argv_append(&argv, soname_arg);
                    }
#endif
                }
            }
        }

        // Print command in verbose mode before executing
        if (ctx->verbose) {
            char cmd_buf[4096];
            int bpos = 0;
            for (int i = 0; i < argv.count && bpos < (int)sizeof(cmd_buf) - 1; ++i) {
                if (i > 0) cmd_buf[bpos++] = ' ';
                if (strchr(argv.args[i], ' ')) {
                    bpos += snprintf(cmd_buf + bpos, sizeof(cmd_buf) - bpos, "'%s'", argv.args[i]);
                } else {
                    bpos += snprintf(cmd_buf + bpos, sizeof(cmd_buf) - bpos, "%s", argv.args[i]);
                }
            }
            cmd_buf[bpos] = '\0';
            cbuild__log(ctx, CBUILD_LOG_VERBOSE, "%s", cmd_buf);
        }

        int rc;
        char* output = NULL;
        rc = cbuild_spawn_process(ctx, &argv, 1, &output);
        cbuild_argv_free(&argv);

        if (output && rc != 0) {
            fwrite(output, 1, strlen(output), stderr);
        }
        if (output)
            free(output);

        if (rc != 0) {
            cbuild__log_status(ctx, 0, "Linking failed for %s", t->output_file);
            *error_flag = 1;
            goto cleanup;
        } else {
            t->rebuilt = 1;
            /* On successful link, write signature of the link command */
            char* link_sig = NULL;
            cbuild_argv_t largv;
            cbuild_argv_init(&largv);

            const char* ld2 = cbuild_target_linker(ctx, t);
            if (t->type == TARGET_STATIC_LIB) {
                cbuild_argv_append(&largv, ctx->ar);
                if (detect_ar_kind(ctx->ar) == CBUILD_CC_MSVC) {
                    char out_arg[1024];
                    snprintf(out_arg, sizeof(out_arg), "/OUT:%s", t->output_file);
                    cbuild_argv_append(&largv, out_arg);
                } else {
                    cbuild_argv_append(&largv, "rcs");
                    cbuild_argv_append(&largv, t->output_file);
                }
                /* object files were already sorted before linking */
                for (int i = 0; i < obj_count; ++i)
                    cbuild_argv_append(&largv, obj_files[i]);

            } else if (t->type == TARGET_EXECUTABLE || t->type == TARGET_SHARED_LIB) {
                cbuild_argv_append(&largv, ld2);

                if (cc_kind == CBUILD_CC_MSVC) {
                    cbuild_argv_append(&largv, "/nologo");
                    char fe_arg[1024];
                    snprintf(fe_arg, sizeof(fe_arg), "/Fe:%s", t->output_file);
                    cbuild_argv_append(&largv, fe_arg);
                } else {
                    cbuild_argv_append(&largv, "-o");
                    cbuild_argv_append(&largv, t->output_file);
                }

                for (int i = 0; i < obj_count; ++i)
                    cbuild_argv_append(&largv, obj_files[i]);

                for (int i = 0; i < t->lib_dir_count; ++i) {
                    char lib_arg[1024];
                    if (cc_kind == CBUILD_CC_MSVC) {
                        snprintf(lib_arg, sizeof(lib_arg), "/LIBPATH:%s", t->lib_dirs[i]);
                    } else {
                        snprintf(lib_arg, sizeof(lib_arg), "-L%s", t->lib_dirs[i]);
                    }
                    cbuild_argv_append(&largv, lib_arg);
                }

                if (t->exposed_lib_count > 0) {
                    if (cc_kind != CBUILD_CC_MSVC) {
#if defined(__linux__) || defined(__unix__) && !defined(__APPLE__)
                        cbuild_argv_append(&largv, "-Wl,-E");
#endif
                    }
                }

                for (int i = 0; i < t->exposed_lib_count; ++i) {
                    cbuild_argv_append(&largv, t->exposed_libs[i]);
                }

                for (int i = 0; i < t->link_lib_count; ++i) {
                    if (cc_kind == CBUILD_CC_MSVC) {
                        /* Skip 'm' (math) library on MSVC - it's part of the CRT */
                        if (strcmp(t->link_libs[i], "m") == 0) continue;
                        if (strchr(t->link_libs[i], '\\') || strchr(t->link_libs[i], ':')) {
                            cbuild_argv_append(&largv, t->link_libs[i]);
                        } else {
                            char lib_arg[512];
                            snprintf(lib_arg, sizeof(lib_arg), "%s.lib", t->link_libs[i]);
                            cbuild_argv_append(&largv, lib_arg);
                        }
                    } else {
                        if (strchr(t->link_libs[i], '/')) {
                            cbuild_argv_append(&largv, t->link_libs[i]);
                        } else {
                            char lib_arg[512];
                            snprintf(lib_arg, sizeof(lib_arg), "-l%s", t->link_libs[i]);
                            cbuild_argv_append(&largv, lib_arg);
                        }
                    }
                }

                for (int i = 0; i < t->dep_count; ++i) {
                    target_t* dep = t->dependencies[i];
                    if (dep->type == TARGET_STATIC_LIB) {
                        cbuild_argv_append(&largv, dep->output_file);
                    } else if (dep->type == TARGET_SHARED_LIB) {
#ifdef _WIN32
                        /* On Windows, link against .lib import library, not .dll */
                        char lib_path[1024];
                        strncpy(lib_path, dep->output_file, sizeof(lib_path) - 1);
                        lib_path[sizeof(lib_path) - 1] = '\0';
                        size_t len = strlen(lib_path);
                        if (len > 4 && strcmp(lib_path + len - 4, ".dll") == 0) {
                            strcpy(lib_path + len - 4, ".lib");
                        }
                        cbuild_argv_append(&largv, lib_path);
#else
                        cbuild_argv_append(&largv, dep->output_file);
#endif
                    } else if (dep->type == TARGET_FILE_DEP && dep->output_file &&
                               cbuild__is_library_path(dep->output_file)) {
                        cbuild_argv_append(&largv, dep->output_file);
                    }
                }

                for (int i = 0; i < t->ldflag_count; ++i)
                    cbuild_argv_append(&largv, t->ldflags[i]);
                for (int i = 0; i < ctx->global_ldflag_count; ++i)
                    cbuild_argv_append(&largv, ctx->global_ldflags[i]);

                if (t->type == TARGET_SHARED_LIB) {
                    if (cc_kind == CBUILD_CC_MSVC) {
                        cbuild_argv_append(&largv, "/LD");
                    } else {
#ifdef __APPLE__
                        cbuild_argv_append(&largv, "-dynamiclib");
                        if (t->soname) {
                            char soname_arg[1024];
                            snprintf(soname_arg, sizeof(soname_arg), "-install_name");
                            cbuild_argv_append(&largv, soname_arg);
                            snprintf(soname_arg, sizeof(soname_arg), "@rpath/%s", t->soname);
                            cbuild_argv_append(&largv, soname_arg);
                        }
#else
                        cbuild_argv_append(&largv, "-shared");
                        if (t->soname) {
                            char soname_arg[1024];
                            snprintf(soname_arg, sizeof(soname_arg), "-Wl,-soname,%s", t->soname);
                            cbuild_argv_append(&largv, soname_arg);
                        }
#endif
                    }
                }
            }

            for (int i = 0; i < largv.count; ++i) {
                append_format(&link_sig, "%s\n", largv.args[i]);
            }
            const char* lev = getenv("LDFLAGS");
            if (lev) append_format(&link_sig, "ENV:LDFLAGS=%s\n", lev);

            char link_sig_path[1024];
            snprintf(link_sig_path, sizeof(link_sig_path), "%s.link.sig", t->output_file);
            FILE* lf = fopen(link_sig_path, "wb");
            if (lf) {
                fwrite(link_sig ? link_sig : "", 1, link_sig ? strlen(link_sig) : 0, lf);
                fclose(lf);
            }
            if (link_sig) free(link_sig);
            cbuild_argv_free(&largv);
        }
    }

cleanup:
    for (int i = 0; i < obj_count; ++i)
        free(obj_files[i]);
    free(obj_files);
}
