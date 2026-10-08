/* cli.c - built-in command-line flags and the cbuild_run entry point. */

#include "cbuild_internal.h"

/* ---------- Flag callback helpers ---------- */

static int cbuild__flag_on_verbose(const char* v, void* u) {
    (void)v;
    cbuild_context_t* ctx = (cbuild_context_t*)u;
    ctx->verbose = 1;
    return 0;
}

static int cbuild__flag_on_target(const char* v, void* u) {
    cbuild_context_t* ctx = (cbuild_context_t*)u;
    if (!v || !*v) {
        cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: --target requires a name");
        return 1;
    }
    ensure_capacity_charpp(ctx, &ctx->target_filters, &ctx->target_filter_count, &ctx->target_filter_cap);
    ctx->target_filters[ctx->target_filter_count++] = strdup(v);
    return 0;
}

static int cbuild__flag_on_compile_commands(const char* v, void* u) {
    (void)v;
    cbuild_context_t* ctx = (cbuild_context_t*)u;
    cbuild_enable_compile_commands(ctx, 1);
    return 0;
}

static int cbuild__flag_on_list(const char* v, void* u) {
    (void)v;
    cbuild_context_t* ctx = (cbuild_context_t*)u;
    char buf[8192];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "Available targets:");
    for (int i = 0; i < ctx->target_count; ++i) {
        target_t* t = ctx->targets[i];
        if (!t->name) continue;
        const char* type_str = "unknown";
        switch (t->type) {
            case TARGET_EXECUTABLE:
                type_str = "executable";
                break;
            case TARGET_STATIC_LIB:
                type_str = "static_lib";
                break;
            case TARGET_SHARED_LIB:
                type_str = "shared_lib";
                break;
            case TARGET_COMMAND:
                type_str = "command";
                break;
            case TARGET_DUMMY:
                type_str = "dummy";
                break;
            case TARGET_FILE_DEP:
                type_str = "file_dep";
                break;
        }
        pos += snprintf(buf + pos, sizeof(buf) - pos, "\n  %-20s  [%s]", t->name, type_str);
        if (t->output_file) pos += snprintf(buf + pos, sizeof(buf) - pos, "  -> %s", t->output_file);
    }
    cbuild__log(ctx, CBUILD_LOG_INFO, "%s", buf);
    return CBUILD_FLAG_EXIT;
}

/* Pretty-print build graph */
static const char* cbuild__type_str(cbuild_target_type tt) {
    switch (tt) {
        case TARGET_EXECUTABLE:
            return "executable";
        case TARGET_STATIC_LIB:
            return "static_lib";
        case TARGET_SHARED_LIB:
            return "shared_lib";
        case TARGET_COMMAND:
            return "command";
        case TARGET_DUMMY:
            return "dummy";
        case TARGET_FILE_DEP:
            return "file_dep";
        default:
            return "unknown";
    }
}

static int cbuild__idx_of_target(cbuild_context_t* ctx, target_t* t) {
    for (int i = 0; i < ctx->target_count; ++i) {
        if (ctx->targets[i] == t) return i;
    }
    return -1;
}

static void cbuild__print_graph_rec(cbuild_context_t* ctx, target_t* t, int* visiting, int depth) {
    int ti = cbuild__idx_of_target(ctx, t);
    if (ti >= 0) {
        if (visiting[ti]) {
            const char* cyc_name = t->name ? t->name : (t->output_file ? t->output_file : "(unnamed)");
            cbuild__log(ctx, CBUILD_LOG_INFO, "%*s-> (cycle to %s)", depth * 2, "", cyc_name);
            return;
        }
        visiting[ti] = 1;
    }

    for (int i = 0; i < t->dep_count; ++i) {
        target_t* dep = t->dependencies[i];
        const char* dep_name = dep->name ? dep->name : (dep->output_file ? dep->output_file : "(unnamed)");
        cbuild__log(ctx, CBUILD_LOG_INFO, "%*s-> %s [%s]", depth * 2, "", dep_name, cbuild__type_str(dep->type));
        cbuild__print_graph_rec(ctx, dep, visiting, depth + 1);
    }

    if (ti >= 0) visiting[ti] = 0;
}

static int cbuild__flag_on_graph(const char* v, void* u) {
    (void)v;
    cbuild_context_t* ctx = (cbuild_context_t*)u;

    cbuild__log(ctx, CBUILD_LOG_INFO, "Build graph:");
    if (ctx->target_count == 0) {
        cbuild__log(ctx, CBUILD_LOG_INFO, "  (no targets)");
        return CBUILD_FLAG_EXIT;
    }

    int* indegree = (int*)calloc(ctx->target_count, sizeof(int));
    if (indegree) {
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
    }

    int* visiting = (int*)calloc(ctx->target_count, sizeof(int));
    if (visiting) {
        int roots = 0;
        for (int i = 0; i < ctx->target_count; ++i) {
            target_t* t = ctx->targets[i];
            /* Skip orphan FILE_DEPs unless something depends on them */
            if (t->type == TARGET_FILE_DEP && indegree && indegree[i] == 0) continue;

            if (!indegree || indegree[i] == 0) {
                const char* name = t->name ? t->name : (t->output_file ? t->output_file : "(unnamed)");
                cbuild__log(ctx, CBUILD_LOG_INFO, "%s [%s]", name, cbuild__type_str(t->type));
                cbuild__print_graph_rec(ctx, t, visiting, 1);
                roots++;
            }
        }
        /* If no roots (cycle-only graph), print all once */
        if (roots == 0) {
            for (int i = 0; i < ctx->target_count; ++i) {
                target_t* t = ctx->targets[i];
                const char* name = t->name ? t->name : (t->output_file ? t->output_file : "(unnamed)");
                cbuild__log(ctx, CBUILD_LOG_INFO, "%s [%s]", name, cbuild__type_str(t->type));
                cbuild__print_graph_rec(ctx, t, visiting, 1);
            }
        }
        free(visiting);
    }
    if (indegree) free(indegree);

    return CBUILD_FLAG_EXIT;
}

/* Reverse dependency (consumers) graph */
static int cbuild__find_target_by_name(cbuild_context_t* ctx, const char* name) {
    if (!name) return -1;
    for (int i = 0; i < ctx->target_count; ++i) {
        target_t* t = ctx->targets[i];
        if (t->name && strcmp(t->name, name) == 0) return i;
    }
    return -1;
}

static void cbuild__print_consumers_rec(cbuild_context_t* ctx, int tgt_idx, int* visiting, int depth) {
    for (int i = 0; i < ctx->target_count; ++i) {
        target_t* t = ctx->targets[i];
        int depends = 0;
        for (int d = 0; d < t->dep_count; ++d) {
            if (t->dependencies[d] == ctx->targets[tgt_idx]) {
                depends = 1;
                break;
            }
        }
        if (!depends) continue;

        if (visiting[i]) {
            const char* cyc_name = t->name ? t->name : (t->output_file ? t->output_file : "(unnamed)");
            cbuild__log(ctx, CBUILD_LOG_INFO, "%*s-> (cycle to %s)", depth * 2, "", cyc_name);
            continue;
        }
        const char* name = t->name ? t->name : (t->output_file ? t->output_file : "(unnamed)");
        cbuild__log(ctx, CBUILD_LOG_INFO, "%*s-> %s [%s]", depth * 2, "", name, cbuild__type_str(t->type));
        visiting[i] = 1;
        cbuild__print_consumers_rec(ctx, i, visiting, depth + 1);
        visiting[i] = 0;
    }
}

static int cbuild__flag_on_deps(const char* v, void* u) {
    cbuild_context_t* ctx = (cbuild_context_t*)u;
    if (!v || !*v) {
        cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: --deps requires a target name");
        return 1;
    }
    int idx = cbuild__find_target_by_name(ctx, v);
    if (idx < 0) {
        cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: target '%s' not found", v);
        return 1;
    }
    target_t* root = ctx->targets[idx];
    const char* name = root->name ? root->name : (root->output_file ? root->output_file : "(unnamed)");
    cbuild__log(ctx, CBUILD_LOG_INFO, "%s [%s]", name, cbuild__type_str(root->type));
    int* visiting = (int*)calloc(ctx->target_count, sizeof(int));
    if (visiting) {
        cbuild__print_consumers_rec(ctx, idx, visiting, 1);
        free(visiting);
    }
    return CBUILD_FLAG_EXIT;
}

static int cbuild__flag_on_manifest(const char* v, void* u) {
    (void)v;
    cbuild_context_t* ctx = (cbuild_context_t*)u;
    for (int i = 0; i < ctx->target_count; ++i) {
        target_t* t = ctx->targets[i];
        if (!t->output_file || !t->name) continue;
        const char* type = NULL;
        switch (t->type) {
            case TARGET_STATIC_LIB:
                type = "static_lib";
                break;
            case TARGET_SHARED_LIB:
                type = "shared_lib";
                break;
            case TARGET_EXECUTABLE:
                type = "executable";
                break;
            default:
                continue;
        }
        cbuild__log(ctx, CBUILD_LOG_INFO, "%s %s %s", type, t->name, t->output_file);
    }
    return CBUILD_FLAG_EXIT;
}

static int cbuild__flag_on_clean(const char* v, void* u) {
    (void)v;
    cbuild_context_t* ctx = (cbuild_context_t*)u;
    cbuild_clean(ctx);
    return CBUILD_FLAG_EXIT;
}

static int cbuild__flag_on_run(const char* v, void* u) {
    cbuild_context_t* ctx = (cbuild_context_t*)u;
    if (!v || !*v) {
        cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: --run needs a name");
        return 1;
    }
    ctx->run_subcmd = v;
    return 0;
}

static int cbuild__flag_on_version(const char* v, void* u) {
    (void)v;
    cbuild_context_t* ctx = (cbuild_context_t*)u;
    cbuild__log(ctx, CBUILD_LOG_INFO, "%s", CBUILD_VERSION);
    return CBUILD_FLAG_EXIT;
}

static int cbuild__flag_on_help(const char* v, void* u) {
    (void)v;
    cbuild_context_t* ctx = (cbuild_context_t*)u;
    cbuild__print_help(ctx);
    return CBUILD_FLAG_EXIT;
}

int cbuild_configure_from_argv(cbuild_context_t* ctx, int argc, char** argv) {
    cbuild_init(ctx);
    ctx->argv0_for_help = (argv && argv[0]) ? argv[0] : "cbuild";

    cbuild_register_flag(ctx, "verbose", 'v', 0, CBUILD_FLAG_PRE, "Verbose output", cbuild__flag_on_verbose, ctx);
    cbuild_register_flag_int(ctx, "jobs", 'j', CBUILD_FLAG_PRE,
                             "Number of parallel compile jobs", &ctx->parallel_jobs);
    cbuild_register_flag(ctx, "target", 't', 1, CBUILD_FLAG_PRE,
                         "Build only the specified target", cbuild__flag_on_target, ctx);
    cbuild_register_flag(ctx, "compile-commands", 0, 0, CBUILD_FLAG_PRE,
                         "Emit compile_commands.json into the output dir", cbuild__flag_on_compile_commands, ctx);
    cbuild_register_flag(ctx, "version", 0, 0, CBUILD_FLAG_PRE, "Show version and exit", cbuild__flag_on_version, ctx);
    cbuild_register_flag(ctx, "help", 'h', 0, CBUILD_FLAG_PRE, "Show help and exit", cbuild__flag_on_help, ctx);

    int rc = cbuild_dispatch_flags_strict(ctx, CBUILD_FLAG_PRE, &argc, &argv);
    if (rc == CBUILD_FLAG_EXIT) return CBUILD_FLAG_EXIT;
    if (rc) return rc;

    if (argc > 1) {
        cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: unexpected argument '%s'", argv[1]);
        return 1;
    }

    return 0;
}

int cbuild_run(cbuild_context_t* ctx, int argc, char** argv) {
    cbuild_init(ctx);

    ctx->argv0_for_help = (argv && argv[0]) ? argv[0] : "cbuild";

    cbuild_register_flag(ctx, "verbose", 'v', 0, CBUILD_FLAG_PRE, "Verbose output", cbuild__flag_on_verbose, ctx);
    cbuild_register_flag_int(ctx, "jobs", 'j', CBUILD_FLAG_PRE,
                             "Number of parallel compile jobs", &ctx->parallel_jobs);
    cbuild_register_flag(ctx, "target", 't', 1, CBUILD_FLAG_PRE,
                         "Build only the specified target", cbuild__flag_on_target, ctx);
    cbuild_register_flag(ctx, "compile-commands", 0, 0, CBUILD_FLAG_PRE,
                         "Emit compile_commands.json into the output dir", cbuild__flag_on_compile_commands, ctx);
    cbuild_register_flag(ctx, "list", 'l', 0, CBUILD_FLAG_PRE,
                         "List targets and exit", cbuild__flag_on_list, ctx);
    cbuild_register_flag(ctx, "graph", 0, 0, CBUILD_FLAG_PRE,
                         "Pretty print build graph and exit", cbuild__flag_on_graph, ctx);
    cbuild_register_flag(ctx, "deps", 0, 1, CBUILD_FLAG_PRE,
                         "Print reverse dependency (consumers) graph for the given target and exit", cbuild__flag_on_deps, ctx);
    cbuild_register_flag(ctx, "manifest", 0, 0, CBUILD_FLAG_PRE,
                         "Print subproject manifest format and exit", cbuild__flag_on_manifest, ctx);
    cbuild_register_flag(ctx, "clean", 0, 0, CBUILD_FLAG_PRE,
                         "Remove build outputs and exit", cbuild__flag_on_clean, ctx);
    cbuild_register_flag(ctx, "run", 'r', 1, CBUILD_FLAG_PRE,
                         "Run a registered subcommand after building its target", cbuild__flag_on_run, ctx);
    cbuild_register_flag(ctx, "version", 0, 0, CBUILD_FLAG_PRE, "Show version and exit", cbuild__flag_on_version, ctx);
    cbuild_register_flag(ctx, "help", 'h', 0, CBUILD_FLAG_PRE, "Show help and exit", cbuild__flag_on_help, ctx);

    int rc = cbuild_dispatch_flags_strict(ctx, CBUILD_FLAG_PRE, &argc, &argv);
    if (rc == CBUILD_FLAG_EXIT) return 0;
    if (rc) return rc;

    cbuild__resolve_target_paths(ctx);

    /* A single bare argument naming a registered subcommand is shorthand for --run=NAME. */
    if (argc == 2 && !ctx->run_subcmd) {
        for (int i = 0; i < ctx->subcommand_count; i++) {
            if (strcmp(ctx->subcommands[i]->name, argv[1]) == 0) {
                ctx->run_subcmd = argv[1];
                argc = 1;
                break;
            }
        }
    }

    if (argc > 1) {
        cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: unexpected argument '%s'", argv[1]);
        cbuild__log(ctx, CBUILD_LOG_INFO, "hint: use flags or a subcommand name (try --help)");
        return 1;
    }

    if (ctx->run_subcmd) {
        int error_flag = 0;

        if (ctx->visited) free(ctx->visited);
        if (ctx->in_stack) free(ctx->in_stack);
        ctx->visited = calloc(ctx->target_count, sizeof(int));
        ctx->in_stack = calloc(ctx->target_count, sizeof(int));

        cbuild_subcommand_t* match = NULL;
        for (int i = 0; i < ctx->subcommand_count; ++i) {
            if (strcmp(ctx->subcommands[i]->name, ctx->run_subcmd) == 0) {
                match = ctx->subcommands[i];
                break;
            }
        }
        if (!match) {
            cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: unknown subcommand '%s'", ctx->run_subcmd);
            free(ctx->visited);
            ctx->visited = NULL;
            free(ctx->in_stack);
            ctx->in_stack = NULL;
            return 1;
        }

        rc = cbuild_dispatch_flags_strict(ctx, CBUILD_FLAG_BEFORE_BUILD, &argc, &argv);
        if (rc == CBUILD_FLAG_EXIT) {
            free(ctx->visited);
            free(ctx->in_stack);
            return 0;
        }
        if (rc) {
            free(ctx->visited);
            free(ctx->in_stack);
            return rc;
        }

        dfs_build_func(ctx, match->target, &error_flag);

        free(ctx->visited);
        ctx->visited = NULL;
        free(ctx->in_stack);
        ctx->in_stack = NULL;

        if (error_flag) {
            cbuild__log_status(ctx, 0, "Build failed.");
            return 1;
        }

        int sub_rc = 0;
        if (match->command_line) {
            cbuild__log_step(ctx, "SUBCMD", CBUILD_COLOR_BLUE, "Running '%s': %s", match->name, match->command_line);
            sub_rc = run_command(ctx, match->command_line, 0, NULL);
        } else if (match->callback) {
            cbuild__log_step(ctx, "SUBCMD", CBUILD_COLOR_BLUE, "Running '%s' (callback)...", match->name);
            match->callback(match->user_data);
        }

        if (sub_rc == 0) {
            rc = cbuild_dispatch_flags_strict(ctx, CBUILD_FLAG_AFTER_BUILD, &argc, &argv);
            if (rc == CBUILD_FLAG_EXIT) return 0;
            if (rc) return rc;
        }
        return sub_rc;
    }

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

    int error_flag = 0;
    if (ctx->visited) free(ctx->visited);
    if (ctx->in_stack) free(ctx->in_stack);
    ctx->visited = calloc(ctx->target_count, sizeof(int));
    ctx->in_stack = calloc(ctx->target_count, sizeof(int));

    rc = cbuild_dispatch_flags_strict(ctx, CBUILD_FLAG_BEFORE_BUILD, &argc, &argv);
    if (rc == CBUILD_FLAG_EXIT) {
        free(ctx->visited);
        free(ctx->in_stack);
        return 0;
    }
    if (rc) {
        free(ctx->visited);
        free(ctx->in_stack);
        return rc;
    }

    /* Collected after BEFORE_BUILD flags so the entries match what gets compiled */
    if (ctx->generate_compile_commands) {
        for (int i = 0; i < ctx->target_count; ++i) {
            collect_compile_commands_for_target(ctx, ctx->targets[i]);
        }
    }

    if (ctx->target_filter_count > 0) {
        for (int f = 0; f < ctx->target_filter_count; ++f) {
            target_t* target_to_build = NULL;
            for (int i = 0; i < ctx->target_count; ++i) {
                if (ctx->targets[i]->name && strcmp(ctx->targets[i]->name, ctx->target_filters[f]) == 0) {
                    target_to_build = ctx->targets[i];
                    break;
                }
            }
            if (!target_to_build) {
                cbuild__log(ctx, CBUILD_LOG_ERROR, "Target '%s' not found", ctx->target_filters[f]);
                cbuild__log(ctx, CBUILD_LOG_INFO, "Use --list to see available targets");
                free(ctx->visited);
                free(ctx->in_stack);
                return 1;
            }
            dfs_build_func(ctx, target_to_build, &error_flag);
            if (error_flag) break;
        }
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
        rc = cbuild_dispatch_flags_strict(ctx, CBUILD_FLAG_AFTER_BUILD, &argc, &argv);
        if (rc == CBUILD_FLAG_EXIT) return 0;
        if (rc) return rc;

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
        return 1;
    }
}
