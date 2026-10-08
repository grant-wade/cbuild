/* context.c - context lifecycle and global settings. */

#include "cbuild_internal.h"

void cbuild_set_user_data(cbuild_context_t* ctx, void* data) {
    if (ctx) ctx->user_data = data;
}

void* cbuild_get_user_data(cbuild_context_t* ctx) {
    return ctx ? ctx->user_data : NULL;
}

/* Context constructor - allocates and initializes a new build context */
cbuild_context_t* cbuild_context_new(void) {
    cbuild_context_t* ctx = (cbuild_context_t*)calloc(1, sizeof(cbuild_context_t));
    if (!ctx) return NULL;

    /* Initialize with defaults */
    ctx->cc_kind = CBUILD_CC_GCC_CLANG;
    ctx->argv0_for_help = "cbuild";

#ifndef _WIN32
    pthread_mutex_init(&ctx->queue_mutex, NULL);
#endif

    return ctx;
}

/* Context destructor - frees all resources owned by the context */
void cbuild_context_free(cbuild_context_t* ctx) {
    if (!ctx) return;

    /* Use cbuild_teardown to free all internal state without reinitializing */
    cbuild_teardown(ctx);

#ifndef _WIN32
    pthread_mutex_destroy(&ctx->queue_mutex);
#endif

    /* Free the context itself */
    free(ctx);
}

void cbuild_enable_compile_commands(cbuild_context_t* ctx, int enabled) {
    ctx->generate_compile_commands = enabled;
}

void cbuild_set_verbose(cbuild_context_t* ctx, int verbose) {
    ctx->verbose = verbose;
}

void cbuild_set_output_dir(cbuild_context_t* ctx, const char* dir) {
    if (ctx->output_dir) {
        free(ctx->output_dir);
    }
    /* Normalize path separators for consistent path handling on Windows */
    ctx->output_dir = cbuild__normalize_path(dir);
}

void cbuild_set_parallelism(cbuild_context_t* ctx, int jobs_count) {
    ctx->parallel_jobs = jobs_count;
}

void cbuild_set_compiler(cbuild_context_t* ctx, const char* compiler_exe) {
    if (ctx->cc) free(ctx->cc);
    ctx->cc = strdup(compiler_exe);
    ctx->cc_kind = detect_cc_kind(compiler_exe);

    if (ctx->ar) free(ctx->ar);
    if (ctx->cc_kind == CBUILD_CC_MSVC) {
        ctx->ar = strdup("lib");
    } else {
        ctx->ar = strdup("ar");
    }

    if (ctx->ld) free(ctx->ld);
    if (ctx->cc_kind == CBUILD_CC_MSVC) {
        ctx->ld = strdup("cl");
    } else {
        ctx->ld = strdup(ctx->cc);
    }
}

void cbuild_set_linker(cbuild_context_t* ctx, const char* linker_exe) {
    if (ctx->ld) free(ctx->ld);
    ctx->ld = strdup(linker_exe);
}

void cbuild_set_archiver(cbuild_context_t* ctx, const char* archiver_exe) {
    if (ctx->ar) free(ctx->ar);
    ctx->ar = strdup(archiver_exe);
}

void cbuild_set_build_type(cbuild_context_t* ctx, const char* build_type) {
    if (!build_type) return;

    // Detect compiler kind if not already set
    if (ctx->cc == NULL) {
        ctx->cc_kind = detect_cc_kind(NULL);
    }

    if (strcmp(build_type, "Debug") == 0) {
        if (ctx->cc_kind == CBUILD_CC_MSVC) {
            // MSVC Debug: /Zi (debug info), /Od (no optimization), /RTC1 (runtime checks)
            cbuild_add_global_cflags(ctx, "/Zi /Od /RTC1");
            cbuild_add_global_ldflags(ctx, "/DEBUG");
        } else {
            // GCC/Clang Debug: -g (debug info), -O0 (no optimization)
            cbuild_add_global_cflags(ctx, "-g -O0");
        }
    } else if (strcmp(build_type, "Release") == 0) {
        if (ctx->cc_kind == CBUILD_CC_MSVC) {
            // MSVC Release: /O2 (optimize for speed), /DNDEBUG
            cbuild_add_global_cflags(ctx, "/O2");
            cbuild_add_global_define(ctx, "NDEBUG");
        } else {
            // GCC/Clang Release: -O3 (aggressive optimization), -DNDEBUG
            cbuild_add_global_cflags(ctx, "-O3");
            cbuild_add_global_define(ctx, "NDEBUG");
        }
    }
    // If build_type is neither "Debug" nor "Release", do nothing
    // This allows users to pass empty string or custom values without error
}

void cbuild_add_global_cflags(cbuild_context_t* ctx, const char* flags) {
    if (!flags) return;
    char* copy = strdup(flags);
    if (!copy) return;

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

            ensure_capacity_charpp(ctx, &ctx->global_cflags, &ctx->global_cflag_count, &ctx->global_cflag_cap);
            ctx->global_cflags[ctx->global_cflag_count++] = strdup(token);

            *p = saved;
        }
    }

    free(copy);
}

void cbuild_add_global_ldflags(cbuild_context_t* ctx, const char* flags) {
    if (!flags) return;
    char* copy = strdup(flags);
    if (!copy) return;

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

            ensure_capacity_charpp(ctx, &ctx->global_ldflags, &ctx->global_ldflag_count, &ctx->global_ldflag_cap);
            ctx->global_ldflags[ctx->global_ldflag_count++] = strdup(token);

            *p = saved;
        }
    }

    free(copy);
}

void cbuild_add_global_define(cbuild_context_t* ctx, const char* macro) {
    if (macro)
        cbuild__add_define_to_list(ctx, &ctx->global_defines, &ctx->global_def_count,
                                   &ctx->global_def_cap, macro, NULL);
}

void cbuild_add_global_define_val(cbuild_context_t* ctx, const char* macro, const char* val) {
    if (macro)
        cbuild__add_define_to_list(ctx, &ctx->global_defines, &ctx->global_def_count,
                                   &ctx->global_def_cap, macro, val);
}

void cbuild_add_global_flag(cbuild_context_t* ctx, const char* flag, int value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", !!value);
    cbuild_add_global_define_val(ctx, flag, buf);
}

CBUILD_INTERNAL void cbuild_init(cbuild_context_t* ctx) {
    if (!ctx->output_dir)
        ctx->output_dir = strdup("build");

    if (!ctx->cc) {
#ifdef _WIN32
        ctx->cc = strdup("cl");
        ctx->cc_kind = CBUILD_CC_MSVC;
#else
        ctx->cc = strdup("cc");
        ctx->cc_kind = CBUILD_CC_GCC_CLANG;
#endif
    } else {
        ctx->cc_kind = detect_cc_kind(ctx->cc);
    }

    if (!ctx->ar) ctx->ar = strdup(ctx->cc_kind == CBUILD_CC_MSVC ? "lib" : "ar");
    if (!ctx->ld) ctx->ld = strdup(ctx->cc_kind == CBUILD_CC_MSVC ? "cl" : ctx->cc);

    if (ctx->parallel_jobs <= 0) {
        int n = 1;
#ifdef _WIN32
        SYSTEM_INFO sysinfo;
        GetSystemInfo(&sysinfo);
        n = sysinfo.dwNumberOfProcessors;
#elif defined(__APPLE__)
        int logical_cpus = 0;
        size_t logical_cpus_size = sizeof(logical_cpus);
        if (sysctlbyname("hw.logicalcpu", &logical_cpus, &logical_cpus_size,
                         NULL, 0) == 0 && logical_cpus > 0) {
            n = logical_cpus;
        }
#else
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        if (cpus > 0)
            n = (int)cpus;
#endif
        ctx->parallel_jobs = n > 0 ? n : 1;
    }
}

/* Internal teardown - frees all resources, leaves ctx in zeroed state */
void cbuild_teardown(cbuild_context_t* ctx) {
    // Free DFS state
    if (ctx->visited) {
        free(ctx->visited);
        ctx->visited = NULL;
    }
    if (ctx->in_stack) {
        free(ctx->in_stack);
        ctx->in_stack = NULL;
    }

    // Free targets
    if (ctx->targets) {
        for (int i = 0; i < ctx->target_count; ++i) {
            target_t* t = ctx->targets[i];
            if (!t) continue;
            if (t->name) free(t->name);

            if (t->sources) {
                for (int j = 0; j < t->sources_count; ++j) free(t->sources[j]);
                free(t->sources);
            }
            if (t->include_dirs) {
                for (int j = 0; j < t->include_count; ++j) free(t->include_dirs[j]);
                free(t->include_dirs);
            }
            if (t->lib_dirs) {
                for (int j = 0; j < t->lib_dir_count; ++j) free(t->lib_dirs[j]);
                free(t->lib_dirs);
            }
            if (t->link_libs) {
                for (int j = 0; j < t->link_lib_count; ++j) free(t->link_libs[j]);
                free(t->link_libs);
            }
            if (t->exposed_libs) {
                for (int j = 0; j < t->exposed_lib_count; ++j) free(t->exposed_libs[j]);
                free(t->exposed_libs);
            }
            if (t->dependencies) free(t->dependencies);
            if (t->cflags) {
                for (int j = 0; j < t->cflag_count; ++j) free(t->cflags[j]);
                free(t->cflags);
            }
            if (t->ldflags) {
                for (int j = 0; j < t->ldflag_count; ++j) free(t->ldflags[j]);
                free(t->ldflags);
            }
            if (t->output_file) free(t->output_file);
            if (t->obj_dir) free(t->obj_dir);
            if (t->commands) free(t->commands);
            if (t->post_commands) free(t->post_commands);
            if (t->defines) {
                for (int j = 0; j < t->define_count; ++j) free(t->defines[j]);
                free(t->defines);
            }
            if (t->soname) free(t->soname);
            if (t->compiler) free(t->compiler);
            if (t->linker) free(t->linker);
            free(t);
        }
        free(ctx->targets);
    }
    ctx->targets = NULL;
    ctx->target_count = 0;
    ctx->target_cap = 0;

    // Free commands
    if (ctx->commands) {
        for (int i = 0; i < ctx->command_count; ++i) {
            command_t* c = ctx->commands[i];
            if (!c) continue;
            if (c->name) free(c->name);
            if (c->command_line) free(c->command_line);
            if (c->argv) {
                for (int j = 0; j < c->argc; ++j) free(c->argv[j]);
                free(c->argv);
            }
            if (c->dependencies) free(c->dependencies);
            free(c);
        }
        free(ctx->commands);
    }
    ctx->commands = NULL;
    ctx->command_count = 0;
    ctx->command_cap = 0;

    // Free subcommands
    if (ctx->subcommands) {
        for (int i = 0; i < ctx->subcommand_count; ++i) {
            cbuild_subcommand_t* sc = ctx->subcommands[i];
            if (!sc) continue;
            if (sc->name) free(sc->name);
            if (sc->command_line) free(sc->command_line);
            // sc->target and sc->user_data are not owned
            free(sc);
        }
        free(ctx->subcommands);
    }
    ctx->subcommands = NULL;
    ctx->subcommand_count = 0;
    ctx->subcommand_cap = 0;

    // Free subprojects
    if (ctx->subprojects) {
        for (int i = 0; i < ctx->subproject_count; ++i) {
            subproject_t* sp = ctx->subprojects[i];
            if (!sp) continue;
            if (sp->alias) free(sp->alias);
            if (sp->directory) free(sp->directory);
            if (sp->cbuild_exe) free(sp->cbuild_exe);
            if (sp->targets) {
                for (int j = 0; j < sp->target_count; ++j) {
                    if (sp->targets[j].name) free(sp->targets[j].name);
                    if (sp->targets[j].type) free(sp->targets[j].type);
                    if (sp->targets[j].output_path) free(sp->targets[j].output_path);
                    // proxy_target is a normal target and freed with ctx->targets
                }
                free(sp->targets);
            }
            // build_cmd is a command_t managed by ctx->commands
            free(sp);
        }
        free(ctx->subprojects);
    }
    ctx->subprojects = NULL;
    ctx->subproject_count = 0;
    ctx->subproject_cap = 0;

    // Free global defines
    if (ctx->global_defines) {
        for (int i = 0; i < ctx->global_def_count; ++i) free(ctx->global_defines[i]);
        free(ctx->global_defines);
    }
    ctx->global_defines = NULL;
    ctx->global_def_count = 0;
    ctx->global_def_cap = 0;

    // Free target filters
    if (ctx->target_filters) {
        for (int i = 0; i < ctx->target_filter_count; ++i) free(ctx->target_filters[i]);
        free(ctx->target_filters);
    }
    ctx->target_filters = NULL;
    ctx->target_filter_count = 0;
    ctx->target_filter_cap = 0;

    // Free compile_commands entries
    if (ctx->cc_entries) {
        for (int i = 0; i < ctx->cc_count; ++i) {
            if (ctx->cc_entries[i].directory) free(ctx->cc_entries[i].directory);
            if (ctx->cc_entries[i].command) free(ctx->cc_entries[i].command);
            if (ctx->cc_entries[i].file) free(ctx->cc_entries[i].file);
            if (ctx->cc_entries[i].arguments) {
                for (int j = 0; j < ctx->cc_entries[i].argc; ++j) {
                    free(ctx->cc_entries[i].arguments[j]);
                }
                free(ctx->cc_entries[i].arguments);
            }
        }
        free(ctx->cc_entries);
    }
    ctx->cc_entries = NULL;
    ctx->cc_count = 0;
    ctx->cc_cap = 0;
    ctx->generate_compile_commands = 0;

    // Free job queue and reset build state
    if (ctx->job_queue) {
        free(ctx->job_queue);
    }
    ctx->job_queue = NULL;
    ctx->job_capacity = 0;
    ctx->job_count = 0;
    ctx->jobs_completed = 0;
    ctx->compiled_count = 0;
    ctx->build_error = 0;

    // Free flag handlers
    if (ctx->flag_handlers) {
        for (int i = 0; i < ctx->flag_count; ++i) {
            if (ctx->flag_handlers[i].long_name) free(ctx->flag_handlers[i].long_name);
            if (ctx->flag_handlers[i].help) free(ctx->flag_handlers[i].help);
            if (ctx->flag_handlers[i].allocated_str) free(ctx->flag_handlers[i].allocated_str);
        }
        free(ctx->flag_handlers);
    }
    ctx->flag_handlers = NULL;
    ctx->flag_count = 0;
    ctx->flag_cap = 0;

    // Reset CLI/run helpers
    ctx->run_subcmd = NULL;
    ctx->argv0_for_help = "cbuild";

    // Free and reset global build settings
    if (ctx->output_dir) {
        free(ctx->output_dir);
        ctx->output_dir = NULL;
    }
    if (ctx->cc) {
        free(ctx->cc);
        ctx->cc = NULL;
    }
    if (ctx->ar) {
        free(ctx->ar);
        ctx->ar = NULL;
    }
    if (ctx->ld) {
        free(ctx->ld);
        ctx->ld = NULL;
    }
    if (ctx->global_cflags) {
        for (int i = 0; i < ctx->global_cflag_count; ++i) free(ctx->global_cflags[i]);
        free(ctx->global_cflags);
        ctx->global_cflags = NULL;
    }
    ctx->global_cflag_count = 0;
    ctx->global_cflag_cap = 0;
    if (ctx->global_ldflags) {
        for (int i = 0; i < ctx->global_ldflag_count; ++i) free(ctx->global_ldflags[i]);
        free(ctx->global_ldflags);
        ctx->global_ldflags = NULL;
    }
    ctx->global_ldflag_count = 0;
    ctx->global_ldflag_cap = 0;
    ctx->parallel_jobs = 0;
    ctx->cc_kind = CBUILD_CC_GCC_CLANG;
    ctx->verbose = 0;

    /* Free config mapping */
    if (ctx->cfg_targets) {
        free(ctx->cfg_targets);
        ctx->cfg_targets = NULL;
    }
    if (ctx->cfg_values) {
        free(ctx->cfg_values);
        ctx->cfg_values = NULL;
    }
    ctx->cfg_count = 0;
    ctx->cfg_cap = 0;
    ctx->active_config = NULL;

    /* Free all tracked configs (includes default_config if it was created via cbuild_config_new) */
    if (ctx->all_configs) {
        for (int i = 0; i < ctx->all_configs_count; ++i) {
            if (ctx->all_configs[i]) {
                cbuild_config_free(ctx, ctx->all_configs[i]);
            }
        }
        free(ctx->all_configs);
        ctx->all_configs = NULL;
    }
    ctx->all_configs_count = 0;
    ctx->all_configs_cap = 0;

    /* Configurations are tracked in all_configs and were released above. */
    ctx->default_config = NULL;

    // Clear error state
    ctx->last_error[0] = '\0';
}

/* Public reset - tears down and reinitializes for reuse */
void cbuild_reset(cbuild_context_t* ctx) {
    cbuild_teardown(ctx);
    // Reinitialize defaults so a fresh graph can be constructed
    cbuild_init(ctx);
}
