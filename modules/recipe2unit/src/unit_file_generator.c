// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "unit_file_generator.h"
#include <errno.h>
#include <fcntl.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/vector.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/recipe.h>
#include <ggl/recipe2unit.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

#define WORKING_DIR_LEN 4096
#define MAX_SCRIPT_SIZE 10000
#define MAX_UNIT_SIZE 10000

#define MAX_RETRIES_BEFORE_BROKEN "3"
#define MAX_RETRIES_INTERVAL_SECONDS "3600"
#define RETRY_DELAY_SECONDS "1"

static GgError concat_script_name_prefix_vec(
    GgMap recipe_map, GgByteVec *script_name_prefix_vec
);

/// Parses [DependencyType] portion of recipe and updates the unit file
/// buffer(out) with dependency information appropriately
static GgError parse_dependency_type(
    GgKV component_dependency, GgByteVec *out
) {
    GgObject *val;
    if (gg_obj_type(*gg_kv_val(&component_dependency)) != GG_TYPE_MAP) {
        GG_LOGE(
            "Any information provided under[ComponentDependencies] section only supports a key value map type."
        );
        return GG_ERR_INVALID;
    }
    if (gg_map_get(
            gg_obj_into_map(*gg_kv_val(&component_dependency)),
            GG_STR("DependencyType"),
            &val
        )) {
        if (gg_obj_type(*val) != GG_TYPE_BUF) {
            return GG_ERR_PARSE;
        }

        if (gg_buffer_eq(GG_STR("HARD"), gg_obj_into_buf(*val))) {
            GgError ret = gg_byte_vec_append(out, GG_STR("BindsTo=ggl."));
            gg_byte_vec_chain_append(
                &ret, out, gg_kv_key(component_dependency)
            );
            gg_byte_vec_chain_append(&ret, out, GG_STR(".service\n"));
            if (ret != GG_ERR_OK) {
                return ret;
            }

        } else {
            GgError ret = gg_byte_vec_append(out, GG_STR("Wants=ggl."));
            gg_byte_vec_chain_append(
                &ret, out, gg_kv_key(component_dependency)
            );
            gg_byte_vec_chain_append(&ret, out, GG_STR(".service\n"));
            if (ret != GG_ERR_OK) {
                return ret;
            }
        }
    }
    return GG_ERR_OK;
}

static GgError dependency_parser(GgObject *dependency_obj, GgByteVec *out) {
    if (gg_obj_type(*dependency_obj) != GG_TYPE_MAP) {
        return GG_ERR_INVALID;
    }
    GgMap dependencies = gg_obj_into_map(*dependency_obj);
    GG_MAP_FOREACH (dep, dependencies) {
        if (gg_obj_type(*gg_kv_val(dep)) == GG_TYPE_MAP) {
            if (gg_buffer_eq(gg_kv_key(*dep), GG_STR("aws.greengrass.Nucleus"))
                || gg_buffer_eq(
                    gg_kv_key(*dep), GG_STR("aws.greengrass.NucleusLite")
                )) {
                GG_LOGD(
                    "Skipping dependency on %.*s for the current unit file",
                    (int) gg_kv_key(*dep).len,
                    gg_kv_key(*dep).data
                );
                continue;
            }

            GgError ret = parse_dependency_type(*dep, out);
            if (ret != GG_ERR_OK) {
                return ret;
            }
        }
        // TODO: deal with version, look conflictsWith
    }

    return GG_ERR_OK;
}

static GgError fill_unit_section(
    GgMap recipe_map, GgByteVec *concat_unit_vector, PhaseSelection phase
) {
    GgError ret = gg_byte_vec_append(concat_unit_vector, GG_STR("[Unit]\n"));
    if (ret != GG_ERR_OK) {
        return ret;
    }
    gg_byte_vec_chain_append(
        &ret,
        concat_unit_vector,
        GG_STR("StartLimitInterval=" MAX_RETRIES_INTERVAL_SECONDS "\n")
    );
    gg_byte_vec_chain_append(
        &ret,
        concat_unit_vector,
        GG_STR("StartLimitBurst=" MAX_RETRIES_BEFORE_BROKEN "\n")
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = gg_byte_vec_append(concat_unit_vector, GG_STR("Description="));

    GgObject *val;
    if (gg_map_get(recipe_map, GG_STR("ComponentDescription"), &val)) {
        if (gg_obj_type(*val) != GG_TYPE_BUF) {
            return GG_ERR_PARSE;
        }

        gg_byte_vec_chain_append(
            &ret, concat_unit_vector, gg_obj_into_buf(*val)
        );
        gg_byte_vec_chain_push(&ret, concat_unit_vector, '\n');
    }

    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = gg_byte_vec_append(
        concat_unit_vector,
        GG_STR(
            "PartOf=greengrass-lite.target\nWants=ggl.core.ggipcd.service\nAfter=ggl.core.ggipcd.service\n"
        )
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (phase == RUN_STARTUP) {
        if (gg_map_get(recipe_map, GG_STR("ComponentDependencies"), &val)) {
            GgObjectType type = gg_obj_type(*val);
            if ((type == GG_TYPE_MAP) || (type == GG_TYPE_LIST)) {
                return dependency_parser(val, concat_unit_vector);
            }
        }
    }

    return GG_ERR_OK;
}

static GgError concat_script_name_prefix_vec(
    GgMap recipe_map, GgByteVec *script_name_prefix_vec
) {
    GgError ret;
    GgObject *component_name;
    if (!gg_map_get(recipe_map, GG_STR("ComponentName"), &component_name)) {
        return GG_ERR_INVALID;
    }
    if (gg_obj_type(*component_name) != GG_TYPE_BUF) {
        return GG_ERR_INVALID;
    }

    // build the script name prefix string
    ret = gg_byte_vec_append(
        script_name_prefix_vec, gg_obj_into_buf(*component_name)
    );
    gg_byte_vec_chain_append(&ret, script_name_prefix_vec, GG_STR(".script."));
    if (ret != GG_ERR_OK) {
        return ret;
    }
    return GG_ERR_OK;
}

static GgError concat_working_dir_vec(
    GgMap recipe_map, GgByteVec *working_dir_vec, Recipe2UnitArgs *args
) {
    GgError ret;
    GgObject *component_name;
    if (!gg_map_get(recipe_map, GG_STR("ComponentName"), &component_name)) {
        return GG_ERR_INVALID;
    }
    if (gg_obj_type(*component_name) != GG_TYPE_BUF) {
        return GG_ERR_INVALID;
    }

    // build the working directory string
    ret = gg_byte_vec_append(
        working_dir_vec, gg_buffer_from_null_term(args->root_dir)
    );
    gg_byte_vec_chain_append(&ret, working_dir_vec, GG_STR("/work/"));
    gg_byte_vec_chain_append(
        &ret, working_dir_vec, gg_obj_into_buf(*component_name)
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return GG_ERR_OK;
}

static GgError concat_exec_start_section_vec(
    GgMap recipe_map,
    GgByteVec *exec_start_section_vec,
    GgObject **component_name,
    Recipe2UnitArgs *args
) {
    GgError ret;
    if (!gg_map_get(recipe_map, GG_STR("ComponentName"), component_name)) {
        return GG_ERR_INVALID;
    }

    if (gg_obj_type(**component_name) != GG_TYPE_BUF) {
        return GG_ERR_INVALID;
    }

    GgObject *component_version_obj;
    if (!gg_map_get(
            recipe_map, GG_STR("ComponentVersion"), &component_version_obj
        )) {
        return GG_ERR_INVALID;
    }

    if (gg_obj_type(*component_version_obj) != GG_TYPE_BUF) {
        return GG_ERR_INVALID;
    }
    GgBuffer component_version = gg_obj_into_buf(*component_version_obj);

    // build the path for ExecStart section in unit file
    ret = gg_byte_vec_append(
        exec_start_section_vec,
        gg_buffer_from_null_term(args->recipe_runner_path)
    );
    gg_byte_vec_chain_append(&ret, exec_start_section_vec, GG_STR(" -n "));
    gg_byte_vec_chain_append(
        &ret, exec_start_section_vec, gg_obj_into_buf(**component_name)
    );
    gg_byte_vec_chain_append(&ret, exec_start_section_vec, GG_STR(" -v "));
    gg_byte_vec_chain_append(&ret, exec_start_section_vec, component_version);
    gg_byte_vec_chain_append(&ret, exec_start_section_vec, GG_STR(" -p "));

    return GG_ERR_OK;
}

static GgError json_pointer_to_buf_list(
    GgBufVec *out_list, GgBuffer json_pointer
) {
    if (json_pointer.len == 0) {
        return GG_ERR_INVALID;
    }
    if (json_pointer.data[0] == '/') {
        json_pointer = gg_buffer_substr(json_pointer, 1, SIZE_MAX);
    }

    while (json_pointer.len > 0) {
        size_t i = 0;
        for (; i != json_pointer.len; ++i) {
            if (json_pointer.data[i] == '/') {
                break;
            }
        }
        GgError ret
            = gg_buf_vec_push(out_list, gg_buffer_substr(json_pointer, 0, i));
        if (ret != GG_ERR_OK) {
            return ret;
        }
        json_pointer = gg_buffer_substr(json_pointer, i + 1, SIZE_MAX);
    }
    return GG_ERR_OK;
}

typedef struct RecipeVariable {
    GgBuffer component_dependency_name;
    GgBuffer variable_type;
    GgBuffer variable_key;
} RecipeVariable;

static GgError expand_timeout(
    GgBuffer *inout_timeout, GgBuffer component_name
) {
    if (inout_timeout == NULL) {
        return GG_ERR_INVALID;
    }
    if (!ggl_is_recipe_variable(*inout_timeout)) {
        return GG_ERR_OK;
    }
    GglRecipeVariable variable = { 0 };
    GgError ret = ggl_parse_recipe_variable(*inout_timeout, &variable);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    if (!gg_buffer_eq(variable.type, GG_STR("configuration"))) {
        GG_LOGE(
            "Timeout recipe variable must come from configuration. (e.g. {configuration:/json/pointer/to/key})"
        );
        return GG_ERR_INVALID;
    }

    uint8_t timeout_config_mem[128] = { 0 };
    GgBuffer timeout_config;
    {
        GgArena alloc = gg_arena_init(GG_BUF(timeout_config_mem));
        GgBuffer key_path[GG_MAX_OBJECT_DEPTH] = { 0 };
        GgBufVec key_path_vec = GG_BUF_VEC(key_path);
        ret = gg_buf_vec_push(&key_path_vec, GG_STR("services"));
        if (variable.component_dependency_name.len > 0) {
            gg_buf_vec_chain_push(
                &ret, &key_path_vec, variable.component_dependency_name
            );
        } else {
            gg_buf_vec_chain_push(&ret, &key_path_vec, component_name);
        }
        gg_buf_vec_chain_push(&ret, &key_path_vec, GG_STR("configuration"));
        if (ret != GG_ERR_OK) {
            return ret;
        }

        ret = json_pointer_to_buf_list(&key_path_vec, variable.key);
        if (ret != GG_ERR_OK) {
            return ret;
        }

        ret = ggl_gg_config_read_str(
            key_path_vec.buf_list, &alloc, &timeout_config
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    if (timeout_config.len > inout_timeout->len) {
        return GG_ERR_NOMEM;
    }
    memcpy(inout_timeout->data, timeout_config.data, timeout_config.len);
    inout_timeout->len = timeout_config.len;
    GG_LOGD(
        "Interpolated Timeout value: \"%.*s\".",
        (int) inout_timeout->len,
        inout_timeout->data
    );

    return GG_ERR_OK;
}

static GgError update_unit_file_buffer(
    GgByteVec *out,
    GgByteVec exec_start_section_vec,
    const char *arg_user,
    const char *arg_group,
    bool is_root,
    GgBuffer selected_phase,
    GgBuffer timeout,
    GgBuffer component_name
) {
    GgError ret = gg_byte_vec_append(out, GG_STR("ExecStart="));
    gg_byte_vec_chain_append(&ret, out, exec_start_section_vec.buf);
    gg_byte_vec_chain_append(&ret, out, selected_phase);
    gg_byte_vec_chain_append(&ret, out, GG_STR("\n"));
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to write ExecStart portion of unit files");
        return ret;
    }

    ret = gg_byte_vec_append(out, GG_STR("SyslogIdentifier="));
    gg_byte_vec_chain_append(&ret, out, component_name);
    gg_byte_vec_chain_append(&ret, out, GG_STR("\n"));
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to write SyslogIdentifier portion of unit files");
        return ret;
    }

    ret = expand_timeout(&timeout, component_name);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to expand timeout variable.");
        return ret;
    }
    if (timeout.len == 0) {
        // The default timeout is 120 seconds
        timeout = GG_STR("120");
    }
    GgBuffer timeout_type = gg_buffer_eq(GG_STR("startup"), selected_phase)
        ? GG_STR("TimeoutStartSec=")
        : GG_STR("TimeoutSec=");
    ret = gg_byte_vec_append(out, timeout_type);
    gg_byte_vec_chain_append(&ret, out, timeout);
    gg_byte_vec_chain_push(&ret, out, '\n');
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (is_root) {
        ret = gg_byte_vec_append(out, GG_STR("User=root\n"));
        gg_byte_vec_chain_append(&ret, out, GG_STR("Group=root\n"));
        if (ret != GG_ERR_OK) {
            return ret;
        }
    } else {
        ret = gg_byte_vec_append(out, GG_STR("User="));
        gg_byte_vec_chain_append(
            &ret, out, gg_buffer_from_null_term((char *) arg_user)
        );
        gg_byte_vec_chain_append(&ret, out, GG_STR("\nGroup="));
        gg_byte_vec_chain_append(
            &ret, out, gg_buffer_from_null_term((char *) arg_group)
        );
        gg_byte_vec_chain_append(&ret, out, GG_STR("\n"));
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    return GG_ERR_OK;
}

static void compatibility_check(GgMap selected_lifecycle_map) {
    if (gg_map_get(selected_lifecycle_map, GG_STR("shutdown"), NULL)) {
        GG_LOGI("'shutdown' phase isn't currently supported by GGLite");
    }
    if (gg_map_get(selected_lifecycle_map, GG_STR("recover"), NULL)) {
        GG_LOGI("'recover' phase isn't currently supported by GGLite");
    }
}

// TODO: Refactor it
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GgError manifest_builder(
    GgMap recipe_map,
    GgByteVec *out,
    GgByteVec exec_start_section_vec,
    Recipe2UnitArgs *args,
    PhaseSelection current_phase
) {
    bool is_root = false;

    GgMap selected_lifecycle_map = { 0 };

    GgError ret = select_linux_lifecycle(recipe_map, &selected_lifecycle_map);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    if (selected_lifecycle_map.len == 0) {
        GG_LOGE("Lifecycle with no phase is not supported");
        return GG_ERR_UNSUPPORTED;
    }

    GgMap set_env_as_map = { 0 };

    GgBuffer lifecycle_script_selection = { 0 };
    GgObject *startup_or_run_section;
    GgObject *obj_for_if_exists;

    if (current_phase == BOOTSTRAP) {
        // Check if there are any unsupported phases first
        // Inside this if block as we do not want to keep on checking on
        // each phase lookup
        compatibility_check(selected_lifecycle_map);

        lifecycle_script_selection = GG_STR("bootstrap");
        gg_byte_vec_chain_append(&ret, out, GG_STR("Type=oneshot\n"));
        gg_byte_vec_chain_append(&ret, out, GG_STR("RemainAfterExit=true\n"));
        gg_byte_vec_chain_append(
            &ret, out, GG_STR("SuccessExitStatus=100 101\n")
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to add unit type information");
            return GG_ERR_FAILURE;
        }
        if (gg_map_get(
                selected_lifecycle_map, GG_STR("bootstrap"), &obj_for_if_exists
            )) {
            if (gg_obj_type(*obj_for_if_exists) == GG_TYPE_LIST) {
                GG_LOGE("bootstrap is a list type");
                return GG_ERR_INVALID;
            }
        } else {
            GG_LOGD("No bootstrap phase found");
            return GG_ERR_NOENTRY;
        }

    } else if (current_phase == INSTALL) {
        lifecycle_script_selection = GG_STR("install");
        gg_byte_vec_chain_append(&ret, out, GG_STR("Type=oneshot\n"));
        gg_byte_vec_chain_append(&ret, out, GG_STR("RemainAfterExit=true\n"));
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to add unit type information");
            return GG_ERR_FAILURE;
        }

        if (gg_map_get(
                selected_lifecycle_map, GG_STR("install"), &obj_for_if_exists
            )) {
            if (gg_obj_type(*obj_for_if_exists) == GG_TYPE_LIST) {
                GG_LOGE("install is a list type");
                return GG_ERR_INVALID;
            }
        } else {
            GG_LOGD("No install phase found");
            return GG_ERR_NOENTRY;
        }

    } else if (current_phase == RUN_STARTUP) {
        if (gg_map_get(
                selected_lifecycle_map,
                GG_STR("startup"),
                &startup_or_run_section
            )) {
            if (gg_obj_type(*startup_or_run_section) == GG_TYPE_LIST) {
                GG_LOGE("'startup' field in the lifecycle is of List type.");
                return GG_ERR_INVALID;
            }
            lifecycle_script_selection = GG_STR("startup");
            ret = gg_byte_vec_append(out, GG_STR("RemainAfterExit=true\n"));
            gg_byte_vec_chain_append(&ret, out, GG_STR("Type=notify\n"));
            // Allow other processes in the cgroup to call sd_pid_notify on
            // the unit's behalf (i.e. gghealthd)
            gg_byte_vec_chain_append(&ret, out, GG_STR("NotifyAccess=all\n"));
            if (ret != GG_ERR_OK) {
                GG_LOGE("Failed to add unit type information");
                return GG_ERR_FAILURE;
            }

        } else if (gg_map_get(
                       selected_lifecycle_map,
                       GG_STR("run"),
                       &startup_or_run_section
                   )) {
            if (gg_obj_type(*startup_or_run_section) == GG_TYPE_LIST) {
                GG_LOGE("'run' field in the lifecycle is of List type.");
                return GG_ERR_INVALID;
            }
            GG_LOGD("Found run phase");
            lifecycle_script_selection = GG_STR("run");
            ret = gg_byte_vec_append(out, GG_STR("Type=exec\n"));
            if (ret != GG_ERR_OK) {
                GG_LOGE("Failed to add unit type information");
                return GG_ERR_FAILURE;
            }
        } else {
            GG_LOGD("No startup or run phase found");
            return GG_ERR_NOENTRY;
        }
    }

    GgBuffer selected_script = { 0 };
    GgBuffer timeout = { 0 };
    ret = fetch_script_section(
        selected_lifecycle_map,
        lifecycle_script_selection,
        &is_root,
        &selected_script,
        &set_env_as_map,
        &timeout
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GgObject *component_name = NULL;
    if (!gg_map_get(recipe_map, GG_STR("ComponentName"), &component_name)) {
        return GG_ERR_INVALID;
    }
    if (gg_obj_type(*component_name) != GG_TYPE_BUF) {
        return GG_ERR_INVALID;
    }
    ret = update_unit_file_buffer(
        out,
        exec_start_section_vec,
        args->user,
        args->group,
        is_root,
        lifecycle_script_selection,
        timeout,
        gg_obj_into_buf(*component_name)
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to write ExecStart portion of unit files");
        return ret;
    }

    return GG_ERR_OK;
}

static GgError fill_install_section(
    GgByteVec *out, PhaseSelection current_phase
) {
    if (current_phase == RUN_STARTUP) {
        GgError ret = gg_byte_vec_append(out, GG_STR("\n[Install]\n"));
        gg_byte_vec_chain_append(
            &ret, out, GG_STR("WantedBy=greengrass-lite.target\n")
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to set Install section to unit file");
            return ret;
        }
    }

    return GG_ERR_OK;
}

static GgError fill_service_section(
    GgMap recipe_map,
    GgByteVec *out,
    Recipe2UnitArgs *args,
    GgObject **component_name,
    PhaseSelection phase
) {
    GgError ret = gg_byte_vec_append(out, GG_STR("[Service]\n"));
    if (ret != GG_ERR_OK) {
        return ret;
    }

    gg_byte_vec_chain_append(&ret, out, GG_STR("Restart=on-failure\n"));
    gg_byte_vec_chain_append(
        &ret, out, GG_STR("RestartSec=" RETRY_DELAY_SECONDS "\n")
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    static uint8_t working_dir_buf[PATH_MAX - 1];
    GgByteVec working_dir_vec = GG_BYTE_VEC(working_dir_buf);

    static uint8_t exec_start_section_buf[2 * WORKING_DIR_LEN];
    GgByteVec exec_start_section_vec = GG_BYTE_VEC(exec_start_section_buf);

    static uint8_t script_name_prefix_buf[PATH_MAX];
    GgByteVec script_name_prefix_vec = GG_BYTE_VEC(script_name_prefix_buf);
    ret = gg_byte_vec_append(&script_name_prefix_vec, GG_STR("ggl."));

    ret = concat_script_name_prefix_vec(recipe_map, &script_name_prefix_vec);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Script Name String prefix concat failed.");
        return ret;
    }
    ret = concat_working_dir_vec(recipe_map, &working_dir_vec, args);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Working directory String prefix concat failed.");
        return ret;
    }
    ret = concat_exec_start_section_vec(
        recipe_map, &exec_start_section_vec, component_name, args
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("ExctStart String prefix concat failed.");
        return ret;
    }

    ret = gg_byte_vec_append(out, GG_STR("WorkingDirectory="));
    gg_byte_vec_chain_append(&ret, out, working_dir_vec.buf);
    gg_byte_vec_chain_append(&ret, out, GG_STR("\n"));
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // Create the working directory if not existant
    int working_dir;
    ret = gg_dir_open(working_dir_vec.buf, O_RDONLY, true, &working_dir);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to created working directory.");
        return ret;
    }
    GG_CLEANUP(cleanup_close, working_dir);

    struct passwd user_info_mem;
    static char user_info_buf[2000];
    struct passwd *user_info = NULL;
    int sys_ret = getpwnam_r(
        args->user,
        &user_info_mem,
        user_info_buf,
        sizeof(user_info_buf),
        &user_info
    );
    if (sys_ret != 0) {
        GG_LOGE("Failed to look up user %s: %d.", args->user, sys_ret);
        return GG_ERR_FAILURE;
    }
    if (user_info == NULL) {
        GG_LOGE("No user with name %s.", args->user);
        return GG_ERR_FAILURE;
    }
    uid_t uid = user_info->pw_uid;

    struct group grp_mem;
    struct group *grp = NULL;
    sys_ret = getgrnam_r(
        args->group, &grp_mem, user_info_buf, sizeof(user_info_buf), &grp
    );
    if (sys_ret != 0) {
        GG_LOGE("Failed to look up group %s: %d.", args->group, sys_ret);
        return GG_ERR_FAILURE;
    }
    if (user_info == NULL) {
        GG_LOGE("No group with name %s.", args->group);
        return GG_ERR_FAILURE;
    }
    gid_t gid = grp->gr_gid;

    sys_ret = fchown(working_dir, uid, gid);
    if (sys_ret != 0) {
        GG_LOGE(
            "Failed to change ownership of %.*s: %d.",
            (int) working_dir_vec.buf.len,
            working_dir_vec.buf.data,
            errno
        );
        return GG_ERR_FAILURE;
    }

    // Add Env Var for GG_root path
    ret = gg_byte_vec_append(
        out,
        GG_STR(
            "Environment=\"AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT="
        )
    );
    gg_byte_vec_chain_append(
        &ret, out, gg_buffer_from_null_term(args->root_dir)
    );
    gg_byte_vec_chain_append(&ret, out, GG_STR("/gg-ipc.socket"));
    gg_byte_vec_chain_append(&ret, out, GG_STR("\"\n"));
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = manifest_builder(
        recipe_map, out, exec_start_section_vec, args, phase
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return GG_ERR_OK;
}

GgError generate_systemd_unit(
    GgMap recipe_map,
    GgBuffer *unit_file_buffer,
    Recipe2UnitArgs *args,
    GgObject **component_name,
    PhaseSelection phase
) {
    GgByteVec concat_unit_vector
        = { .buf = { .data = unit_file_buffer->data, .len = 0 },
            .capacity = MAX_UNIT_SIZE };

    GgError ret = fill_unit_section(recipe_map, &concat_unit_vector, phase);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = gg_byte_vec_append(&concat_unit_vector, GG_STR("\n"));
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = fill_service_section(
        recipe_map, &concat_unit_vector, args, component_name, phase
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = fill_install_section(&concat_unit_vector, phase);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    *unit_file_buffer = concat_unit_vector.buf;
    return GG_ERR_OK;
}
