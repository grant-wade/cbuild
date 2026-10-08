/* subproject.c - subproject manifests and imported targets. */

#include "cbuild_internal.h"

static void cbuild__parse_manifest(cbuild_context_t* ctx, subproject_t* sub) {
    if (sub->manifest_loaded)
        return;

    /* Save current directory and change to subproject directory */
    char old_cwd[PATH_MAX];
    if (!getcwd(old_cwd, sizeof(old_cwd))) {
        cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: Failed to get current directory");
        return;
    }

    if (chdir(sub->directory) != 0) {
        cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: Failed to change to subproject directory '%s'", sub->directory);
        return;
    }

    /* Build argv for manifest command */
    cbuild_argv_t argv;
    cbuild_argv_init(&argv);
    cbuild_argv_append(&argv, sub->cbuild_exe);
    cbuild_argv_append(&argv, "--manifest");

    char* output = NULL;
    int result = cbuild_spawn_process(ctx, &argv, 1, &output);
    cbuild_argv_free(&argv);

    /* Restore original directory */
    chdir(old_cwd);

    if (result != 0 || !output) {
        cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: Failed to get manifest from subproject '%s'", sub->alias);
        if (output)
            free(output);
        return;
    }

    char* saveptr = NULL;
    char* line = strtok_r(output, "\r\n", &saveptr);

    while (line) {
        cbuild__trim(line);
        if (!line[0] || line[0] == '#') {
            line = strtok_r(NULL, "\r\n", &saveptr);
            continue;
        }

        char* line_copy = strdup(line);
        char* type = strtok(line_copy, " \t");
        char* name = strtok(NULL, " \t");
        char* path = strtok(NULL, "\r\n");

        if (type && name && path) {
            cbuild__trim(path);
            if (sub->target_count + 1 > sub->target_cap) {
                sub->target_cap = sub->target_cap ? sub->target_cap * 2 : 4;
                sub->targets = realloc(
                    sub->targets, sub->target_cap * sizeof(cbuild_subproject_target_t));
            }
            sub->targets[sub->target_count].name = strdup(name);
            sub->targets[sub->target_count].type = strdup(type);
            sub->targets[sub->target_count].output_path = strdup(path);
            sub->targets[sub->target_count].proxy_target = NULL;
            sub->target_count++;
        }

        free(line_copy);
        line = strtok_r(NULL, "\r\n", &saveptr);
    }

    free(output);
    sub->manifest_loaded = 1;
}

static cbuild_subproject_target_t*
cbuild__find_subproject_target(subproject_t* sub, const char* tgt_name) {
    cbuild__parse_manifest(NULL, sub);
    for (int i = 0; i < sub->target_count; ++i) {
        if (strcmp(sub->targets[i].name, tgt_name) == 0) {
            return &sub->targets[i];
        }
    }
    return NULL;
}

subproject_t* cbuild_add_subproject(cbuild_context_t* ctx, const char* alias, const char* directory,
                                    const char* cbuild_exe) {
    subproject_t* sub = (subproject_t*)calloc(1, sizeof(subproject_t));
    sub->alias = strdup(alias);
    sub->directory = strdup(directory);
    sub->cbuild_exe = strdup(cbuild_exe);

    char* cmdline = NULL;
#ifdef _WIN32
    /* Shell commands use PowerShell on Windows. */
    append_format(&cmdline, "Set-Location -LiteralPath '%s'; & '%s'", directory, cbuild_exe);
#else
    append_format(&cmdline, "cd '%s' && '%s'", directory, cbuild_exe);
#endif
    char build_cmd_name[256];
    snprintf(build_cmd_name, sizeof(build_cmd_name), "build subproject %s",
             alias);
    sub->build_cmd = cbuild_command(ctx, build_cmd_name, cmdline);
    free(cmdline);

    if (ctx->subproject_count + 1 > ctx->subproject_cap) {
        ctx->subproject_cap = ctx->subproject_cap ? ctx->subproject_cap * 2 : 4;
        ctx->subprojects =
            realloc(ctx->subprojects, ctx->subproject_cap * sizeof(subproject_t*));
    }
    ctx->subprojects[ctx->subproject_count++] = sub;
    return sub;
}

target_t* cbuild_subproject_get_target(cbuild_context_t* ctx, subproject_t* sub,
                                       const char* tgt_name) {
    if (!sub)
        return NULL;
    cbuild_subproject_target_t* stgt =
        cbuild__find_subproject_target(sub, tgt_name);
    if (!stgt) {
        cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: Subproject '%s' has no target named '%s'", sub->alias, tgt_name);
        return NULL;
    }
    if (stgt->proxy_target)
        return stgt->proxy_target;

    cbuild_target_type type;
    if (strcmp(stgt->type, "static_lib") == 0) {
        type = TARGET_STATIC_LIB;
    } else if (strcmp(stgt->type, "shared_lib") == 0) {
        type = TARGET_SHARED_LIB;
    } else if (strcmp(stgt->type, "executable") == 0) {
        type = TARGET_EXECUTABLE;
    } else {
        cbuild__log(ctx, CBUILD_LOG_ERROR, "cbuild: Unknown subproject target type: %s", stgt->type);
        return NULL;
    }

    char proxy_name[256];
    snprintf(proxy_name, sizeof(proxy_name), "%s_%s", sub->alias, stgt->name);
    target_t* proxy = (target_t*)calloc(1, sizeof(target_t));
    proxy->type = type;
    proxy->name = strdup(proxy_name);
    proxy->external = 1;

    proxy->output_file = cbuild__join_path(sub->directory, stgt->output_path);
    proxy->obj_dir = NULL;  // not used

    proxy->commands = NULL;
    proxy->cmd_count = proxy->cmd_cap = 0;
    cbuild_target_add_command(ctx, proxy, sub->build_cmd);

    ensure_capacity_charpp(ctx, (char***)&ctx->targets, &ctx->target_count, &ctx->target_cap);
    ctx->targets[ctx->target_count++] = proxy;

    stgt->proxy_target = proxy;
    return proxy;
}
