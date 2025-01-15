// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "unit_file_generator.h"
#include "ggl/recipe2unit.h"
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/recipe.h>
#include <ggl/vector.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

#define WORKING_DIR_LEN 4096
#define MAX_SCRIPT_SIZE 10000
#define MAX_UNIT_SIZE 10000

#define MAX_RETRIES_BEFORE_BROKEN "3"
#define MAX_RETRIES_INTERVAL_SECONDS "3600"
#define RETRY_DELAY_SECONDS "1"

static GglError concat_script_name_prefix_vec(
    const GglMap *recipe_map, GglByteVec *script_name_prefix_vec
);

/// Parses [DependencyType] portion of recipe and updates the unit file
/// buffer(out) with dependency information appropriately
static GglError parse_dependency_type(
    GglKV component_dependency, GglByteVec *out
) {
    GglObject *val;
    if (component_dependency.val.type != GGL_TYPE_MAP) {
        GGL_LOGE(
            "Any information provided under[ComponentDependencies] section "
            "only supports a key value map type."
        );
        return GGL_ERR_INVALID;
    }
    if (ggl_map_get(
            component_dependency.val.map, GGL_STR("DependencyType"), &val
        )) {
        if (val->type != GGL_TYPE_BUF) {
            return GGL_ERR_PARSE;
        }

        if (ggl_buffer_eq(GGL_STR("HARD"), val->buf)) {
            GglError ret = ggl_byte_vec_append(out, GGL_STR("BindsTo=ggl."));
            ggl_byte_vec_chain_append(&ret, out, component_dependency.key);
            ggl_byte_vec_chain_append(&ret, out, GGL_STR(".service\n"));
            if (ret != GGL_ERR_OK) {
                return ret;
            }

        } else {
            GglError ret = ggl_byte_vec_append(out, GGL_STR("Wants=ggl."));
            ggl_byte_vec_chain_append(&ret, out, component_dependency.key);
            ggl_byte_vec_chain_append(&ret, out, GGL_STR(".service\n"));
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        }
    }
    return GGL_ERR_OK;
}

static GglError dependency_parser(GglObject *dependency_obj, GglByteVec *out) {
    if (dependency_obj->type != GGL_TYPE_MAP) {
        return GGL_ERR_INVALID;
    }
    for (size_t count = 0; count < dependency_obj->map.len; count++) {
        if (dependency_obj->map.pairs[count].val.type == GGL_TYPE_MAP) {
            if (ggl_buffer_eq(
                    dependency_obj->map.pairs[count].key,
                    GGL_STR("aws.greengrass.Nucleus")
                )
                || ggl_buffer_eq(
                    dependency_obj->map.pairs[count].key,
                    GGL_STR("aws.greengrass.NucleusLite")
                )) {
                GGL_LOGD(
                    "Skipping dependency on %.*s for the current unit file",
                    (int) dependency_obj->map.pairs[count].key.len,
                    dependency_obj->map.pairs[count].key.data
                );
                continue;
            }

            GglError ret
                = parse_dependency_type(dependency_obj->map.pairs[count], out);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        }
        // TODO: deal with version, look conflictsWith
    }

    return GGL_ERR_OK;
}

static GglError fill_unit_section(
    GglMap recipe_map, GglByteVec *concat_unit_vector, PhaseSelection phase
) {
    GglObject *val;

    GglError ret = ggl_byte_vec_append(concat_unit_vector, GGL_STR("[Unit]\n"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    ggl_byte_vec_chain_append(
        &ret,
        concat_unit_vector,
        GGL_STR("StartLimitInterval=" MAX_RETRIES_INTERVAL_SECONDS "\n")
    );
    ggl_byte_vec_chain_append(
        &ret,
        concat_unit_vector,
        GGL_STR("StartLimitBurst=" MAX_RETRIES_BEFORE_BROKEN "\n")
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_byte_vec_append(concat_unit_vector, GGL_STR("Description="));
    if (ggl_map_get(recipe_map, GGL_STR("ComponentDescription"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            return GGL_ERR_PARSE;
        }

        ggl_byte_vec_chain_append(&ret, concat_unit_vector, val->buf);
        ggl_byte_vec_chain_push(&ret, concat_unit_vector, '\n');
    }
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (phase == RUN_STARTUP) {
        ret = ggl_byte_vec_append(
            concat_unit_vector,
            GGL_STR(
                "Wants=ggl.core.ggipcd.service\nAfter=ggl.core.ggipcd.service\n"
            )
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        if (ggl_map_get(recipe_map, GGL_STR("ComponentDependencies"), &val)) {
            if ((val->type == GGL_TYPE_MAP) || (val->type == GGL_TYPE_LIST)) {
                return dependency_parser(val, concat_unit_vector);
            }
        }
    }

    return GGL_ERR_OK;
}

static GglError concat_script_name_prefix_vec(
    const GglMap *recipe_map, GglByteVec *script_name_prefix_vec
) {
    GglError ret;
    GglObject *component_name;
    if (!ggl_map_get(*recipe_map, GGL_STR("ComponentName"), &component_name)) {
        return GGL_ERR_INVALID;
    }
    if (component_name->type != GGL_TYPE_BUF) {
        return GGL_ERR_INVALID;
    }

    // build the script name prefix string
    ret = ggl_byte_vec_append(script_name_prefix_vec, component_name->buf);
    ggl_byte_vec_chain_append(
        &ret, script_name_prefix_vec, GGL_STR(".script.")
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    return GGL_ERR_OK;
}

static GglError concat_working_dir_vec(
    const GglMap *recipe_map, GglByteVec *working_dir_vec, Recipe2UnitArgs *args

) {
    GglError ret;
    GglObject *component_name;
    if (!ggl_map_get(*recipe_map, GGL_STR("ComponentName"), &component_name)) {
        return GGL_ERR_INVALID;
    }
    if (component_name->type != GGL_TYPE_BUF) {
        return GGL_ERR_INVALID;
    }

    // build the working directory string
    ret = ggl_byte_vec_append(
        working_dir_vec, ggl_buffer_from_null_term(args->root_dir)
    );
    ggl_byte_vec_chain_append(&ret, working_dir_vec, GGL_STR("/work/"));
    ggl_byte_vec_chain_append(&ret, working_dir_vec, component_name->buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}

static GglError concat_exec_start_section_vec(
    const GglMap *recipe_map,
    GglByteVec *exec_start_section_vec,
    GglObject **component_name,
    Recipe2UnitArgs *args
) {
    GglError ret;
    if (!ggl_map_get(*recipe_map, GGL_STR("ComponentName"), component_name)) {
        return GGL_ERR_INVALID;
    }

    if ((*component_name)->type != GGL_TYPE_BUF) {
        return GGL_ERR_INVALID;
    }

    GglObject *component_version_obj;
    if (!ggl_map_get(
            *recipe_map, GGL_STR("ComponentVersion"), &component_version_obj
        )) {
        return GGL_ERR_INVALID;
    }

    if (component_version_obj->type != GGL_TYPE_BUF) {
        return GGL_ERR_INVALID;
    }
    GglBuffer component_version = component_version_obj->buf;

    // build the path for ExecStart section in unit file
    ret = ggl_byte_vec_append(
        exec_start_section_vec,
        ggl_buffer_from_null_term(args->recipe_runner_path)
    );
    ggl_byte_vec_chain_append(&ret, exec_start_section_vec, GGL_STR(" -n "));
    ggl_byte_vec_chain_append(
        &ret, exec_start_section_vec, (*component_name)->buf
    );
    ggl_byte_vec_chain_append(&ret, exec_start_section_vec, GGL_STR(" -v "));
    ggl_byte_vec_chain_append(&ret, exec_start_section_vec, component_version);
    ggl_byte_vec_chain_append(&ret, exec_start_section_vec, GGL_STR(" -p "));

    return GGL_ERR_OK;
}

static GglError json_pointer_to_buf_list(
    GglBufVec *out_list, GglBuffer json_pointer
) {
    if (json_pointer.len == 0) {
        return GGL_ERR_INVALID;
    }
    if (json_pointer.data[0] == '/') {
        json_pointer = ggl_buffer_substr(json_pointer, 1, SIZE_MAX);
    }

    while (json_pointer.len > 0) {
        size_t i = 0;
        for (; i != json_pointer.len; ++i) {
            if (json_pointer.data[i] == '/') {
                break;
            }
        }
        GglError ret
            = ggl_buf_vec_push(out_list, ggl_buffer_substr(json_pointer, 0, i));
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        json_pointer = ggl_buffer_substr(json_pointer, i + 1, SIZE_MAX);
    }
    return GGL_ERR_OK;
}

typedef struct RecipeVariable {
    GglBuffer component_dependency_name;
    GglBuffer variable_type;
    GglBuffer variable_key;
} RecipeVariable;

static GglError expand_timeout(
    GglBuffer *inout_timeout, GglBuffer component_name
) {
    if (inout_timeout == NULL) {
        return GGL_ERR_INVALID;
    }
    if (!ggl_is_recipe_variable(*inout_timeout)) {
        return GGL_ERR_OK;
    }
    GglRecipeVariable variable = { 0 };
    GglError ret = ggl_parse_recipe_variable(*inout_timeout, &variable);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    if (!ggl_buffer_eq(variable.type, GGL_STR("configuration"))) {
        GGL_LOGE("Timeout recipe variable must come from configuration. (e.g. "
                 "{configuration:/json/pointer/to/key})");
        return GGL_ERR_INVALID;
    }

    uint8_t timeout_config_buf[128] = { 0 };
    GglBuffer timeout_config = GGL_BUF(timeout_config_buf);
    {
        GglBuffer key_path[15] = { 0 };
        GglBufVec key_path_vec = GGL_BUF_VEC(key_path);
        ggl_buf_vec_push(&key_path_vec, GGL_STR("services"));
        if (variable.component_dependency_name.len > 0) {
            ggl_buf_vec_push(&key_path_vec, variable.component_dependency_name);
        } else {
            ggl_buf_vec_push(&key_path_vec, component_name);
        }

        ret = json_pointer_to_buf_list(&key_path_vec, variable.key);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        ret = ggl_gg_config_read_str(key_path_vec.buf_list, &timeout_config);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    if (timeout_config.len > inout_timeout->len) {
        return GGL_ERR_NOMEM;
    }
    memcpy(inout_timeout->data, timeout_config.data, timeout_config.len);
    inout_timeout->len = timeout_config.len;
    GGL_LOGD(
        "Interpolated Timeout value: \"%.*s\".",
        (int) inout_timeout->len,
        inout_timeout->data
    );

    return GGL_ERR_OK;
}

static GglError update_unit_file_buffer(
    GglByteVec *out,
    GglByteVec exec_start_section_vec,
    char *arg_user,
    char *arg_group,
    bool is_root,
    GglBuffer selected_phase,
    GglBuffer timeout,
    GglBuffer component_name
) {
    GglError ret = ggl_byte_vec_append(out, GGL_STR("ExecStart="));
    ggl_byte_vec_chain_append(&ret, out, exec_start_section_vec.buf);
    ggl_byte_vec_chain_append(&ret, out, selected_phase);
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("\n"));
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to write ExecStart portion of unit files");
        return ret;
    }

    ret = expand_timeout(&timeout, component_name);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to expand timeout variable.");
        return ret;
    }
    if (timeout.len == 0) {
        // The default timeout is 120 seconds
        timeout = GGL_STR("120");
    }
    GglBuffer timeout_type = ggl_buffer_eq(GGL_STR("startup"), selected_phase)
        ? GGL_STR("TimeoutStartSec=")
        : GGL_STR("TimeoutSec=");
    ret = ggl_byte_vec_append(out, timeout_type);
    ggl_byte_vec_chain_append(&ret, out, timeout);
    ggl_byte_vec_chain_push(&ret, out, '\n');
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (is_root) {
        ret = ggl_byte_vec_append(out, GGL_STR("User=root\n"));
        ggl_byte_vec_chain_append(&ret, out, GGL_STR("Group=root\n"));
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    } else {
        ret = ggl_byte_vec_append(out, GGL_STR("User="));
        ggl_byte_vec_chain_append(
            &ret, out, ggl_buffer_from_null_term(arg_user)
        );
        ggl_byte_vec_chain_append(&ret, out, GGL_STR("\nGroup="));
        ggl_byte_vec_chain_append(
            &ret, out, ggl_buffer_from_null_term(arg_group)
        );
        ggl_byte_vec_chain_append(&ret, out, GGL_STR("\n"));
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}

static void compatibility_check(GglMap selected_lifecycle_map) {
    if (ggl_map_get(selected_lifecycle_map, GGL_STR("shutdown"), NULL)) {
        GGL_LOGI("'shutdown' phase isn't currently supported by GGLite");
    }
    if (ggl_map_get(selected_lifecycle_map, GGL_STR("recover"), NULL)) {
        GGL_LOGI("'recover' phase isn't currently supported by GGLite");
    }
}

// TODO: Refactor it
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GglError manifest_builder(
    GglMap recipe_map,
    GglByteVec *out,
    GglByteVec exec_start_section_vec,
    Recipe2UnitArgs *args,
    PhaseSelection current_phase
) {
    bool is_root = false;

    GglMap selected_lifecycle_map = { 0 };

    GglError ret = select_linux_lifecycle(recipe_map, &selected_lifecycle_map);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    if (selected_lifecycle_map.len == 0) {
        GGL_LOGE("Lifecycle with no phase is not supported");
        return GGL_ERR_UNSUPPORTED;
    }

    GglMap set_env_as_map = { 0 };

    GglBuffer lifecycle_script_selection = { 0 };
    GglObject *startup_or_run_section;
    GglObject *obj_for_if_exists;

    if (current_phase == BOOTSTRAP) {
        // Check if there are any unsupported phases first
        // Inside this if block as we do not want to keep on checking on
        // each phase lookup
        compatibility_check(selected_lifecycle_map);

        lifecycle_script_selection = GGL_STR("bootstrap");
        ggl_byte_vec_chain_append(&ret, out, GGL_STR("Type=oneshot\n"));
        ggl_byte_vec_chain_append(&ret, out, GGL_STR("RemainAfterExit=true\n"));
        ggl_byte_vec_chain_append(
            &ret, out, GGL_STR("SuccessExitStatus=100 101\n")
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to add unit type information");
            return GGL_ERR_FAILURE;
        }
        if (ggl_map_get(
                selected_lifecycle_map, GGL_STR("bootstrap"), &obj_for_if_exists
            )) {
            if (obj_for_if_exists->type == GGL_TYPE_LIST) {
                GGL_LOGE("bootstrap is a list type");
                return GGL_ERR_INVALID;
            }
        } else {
            GGL_LOGD("No bootstrap phase found");
            return GGL_ERR_NOENTRY;
        }

    } else if (current_phase == INSTALL) {
        lifecycle_script_selection = GGL_STR("install");
        ggl_byte_vec_chain_append(&ret, out, GGL_STR("Type=oneshot\n"));
        ggl_byte_vec_chain_append(&ret, out, GGL_STR("RemainAfterExit=true\n"));
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to add unit type information");
            return GGL_ERR_FAILURE;
        }

        if (ggl_map_get(
                selected_lifecycle_map, GGL_STR("install"), &obj_for_if_exists
            )) {
            if (obj_for_if_exists->type == GGL_TYPE_LIST) {
                GGL_LOGE("install is a list type");
                return GGL_ERR_INVALID;
            }
        } else {
            GGL_LOGD("No install phase found");
            return GGL_ERR_NOENTRY;
        }

    } else if (current_phase == RUN_STARTUP) {
        if (ggl_map_get(
                selected_lifecycle_map,
                GGL_STR("startup"),
                &startup_or_run_section
            )) {
            if (startup_or_run_section->type == GGL_TYPE_LIST) {
                GGL_LOGE("'startup' field in the lifecycle is of List type.");
                return GGL_ERR_INVALID;
            }
            lifecycle_script_selection = GGL_STR("startup");
            ret = ggl_byte_vec_append(out, GGL_STR("RemainAfterExit=true\n"));
            ggl_byte_vec_chain_append(&ret, out, GGL_STR("Type=notify\n"));
            // Allow other processes in the cgroup to call sd_pid_notify on
            // the unit's behalf (i.e. gghealthd)
            ggl_byte_vec_chain_append(&ret, out, GGL_STR("NotifyAccess=all\n"));
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to add unit type information");
                return GGL_ERR_FAILURE;
            }

        } else if (ggl_map_get(
                       selected_lifecycle_map,
                       GGL_STR("run"),
                       &startup_or_run_section
                   )) {
            if (startup_or_run_section->type == GGL_TYPE_LIST) {
                GGL_LOGE("'run' field in the lifecycle is of List type.");
                return GGL_ERR_INVALID;
            }
            GGL_LOGD("Found run phase");
            lifecycle_script_selection = GGL_STR("run");
            ret = ggl_byte_vec_append(out, GGL_STR("Type=exec\n"));
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to add unit type information");
                return GGL_ERR_FAILURE;
            }
        } else {
            GGL_LOGD("No startup or run phase found");
            return GGL_ERR_NOENTRY;
        }
    }

    GglBuffer selected_script = { 0 };
    GglBuffer timeout = { 0 };
    ret = fetch_script_section(
        selected_lifecycle_map,
        lifecycle_script_selection,
        &is_root,
        &selected_script,
        &set_env_as_map,
        &timeout
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglObject *component_name = NULL;
    if (!ggl_map_get(recipe_map, GGL_STR("ComponentName"), &component_name)) {
        return GGL_ERR_INVALID;
    }
    if (component_name->type != GGL_TYPE_BUF) {
        return GGL_ERR_INVALID;
    }
    ret = update_unit_file_buffer(
        out,
        exec_start_section_vec,
        args->user,
        args->group,
        is_root,
        lifecycle_script_selection,
        timeout,
        component_name->buf
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to write ExecStart portion of unit files");
        return ret;
    }

    return GGL_ERR_OK;
}

static GglError fill_install_section(
    GglByteVec *out, PhaseSelection current_phase
) {
    if (current_phase == RUN_STARTUP) {
        GglError ret = ggl_byte_vec_append(out, GGL_STR("\n[Install]\n"));
        ggl_byte_vec_chain_append(
            &ret, out, GGL_STR("WantedBy=greengrass-lite.target\n")
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to set Install section to unit file");
            return ret;
        }
    }

    return GGL_ERR_OK;
}

static GglError fill_service_section(
    const GglMap *recipe_map,
    GglByteVec *out,
    Recipe2UnitArgs *args,
    GglObject **component_name,
    PhaseSelection phase
) {
    GglError ret = ggl_byte_vec_append(out, GGL_STR("[Service]\n"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ggl_byte_vec_chain_append(&ret, out, GGL_STR("Restart=on-failure\n"));
    ggl_byte_vec_chain_append(
        &ret, out, GGL_STR("RestartSec=" RETRY_DELAY_SECONDS "\n")
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t working_dir_buf[PATH_MAX - 1];
    GglByteVec working_dir_vec = GGL_BYTE_VEC(working_dir_buf);

    static uint8_t exec_start_section_buf[2 * WORKING_DIR_LEN];
    GglByteVec exec_start_section_vec = GGL_BYTE_VEC(exec_start_section_buf);

    static uint8_t script_name_prefix_buf[PATH_MAX];
    GglByteVec script_name_prefix_vec = GGL_BYTE_VEC(script_name_prefix_buf);
    ret = ggl_byte_vec_append(&script_name_prefix_vec, GGL_STR("ggl."));

    ret = concat_script_name_prefix_vec(recipe_map, &script_name_prefix_vec);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Script Name String prefix concat failed.");
        return ret;
    }
    ret = concat_working_dir_vec(recipe_map, &working_dir_vec, args);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Working directory String prefix concat failed.");
        return ret;
    }
    ret = concat_exec_start_section_vec(
        recipe_map, &exec_start_section_vec, component_name, args
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("ExctStart String prefix concat failed.");
        return ret;
    }

    ret = ggl_byte_vec_append(out, GGL_STR("WorkingDirectory="));
    ggl_byte_vec_chain_append(&ret, out, working_dir_vec.buf);
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("\n"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // Create the working directory if not existant
    int working_dir;
    ret = ggl_dir_open(working_dir_vec.buf, O_RDONLY, true, &working_dir);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to created working directory.");
        return ret;
    }
    GGL_CLEANUP(cleanup_close, working_dir);

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
        GGL_LOGE("Failed to look up user %s: %d.", args->user, sys_ret);
        return GGL_ERR_FAILURE;
    }
    if (user_info == NULL) {
        GGL_LOGE("No user with name %s.", args->user);
        return GGL_ERR_FAILURE;
    }
    uid_t uid = user_info->pw_uid;

    struct group grp_mem;
    struct group *grp = NULL;
    sys_ret = getgrnam_r(
        args->group, &grp_mem, user_info_buf, sizeof(user_info_buf), &grp
    );
    if (sys_ret != 0) {
        GGL_LOGE("Failed to look up group %s: %d.", args->group, sys_ret);
        return GGL_ERR_FAILURE;
    }
    if (user_info == NULL) {
        GGL_LOGE("No group with name %s.", args->group);
        return GGL_ERR_FAILURE;
    }
    gid_t gid = grp->gr_gid;

    sys_ret = fchown(working_dir, uid, gid);
    if (sys_ret != 0) {
        GGL_LOGE(
            "Failed to change ownership of %.*s: %d.",
            (int) working_dir_vec.buf.len,
            working_dir_vec.buf.data,
            errno
        );
        return GGL_ERR_FAILURE;
    }

    // Add Env Var for GG_root path
    ret = ggl_byte_vec_append(
        out,
        GGL_STR("Environment=\"AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_"
                "COMPONENT=")
    );
    ggl_byte_vec_chain_append(
        &ret, out, ggl_buffer_from_null_term(args->root_dir)
    );
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("/gg-ipc.socket"));
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("\"\n"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = manifest_builder(
        *recipe_map, out, exec_start_section_vec, args, phase
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}

GglError generate_systemd_unit(
    const GglMap *recipe_map,
    GglBuffer *unit_file_buffer,
    Recipe2UnitArgs *args,
    GglObject **component_name,
    PhaseSelection phase
) {
    GglByteVec concat_unit_vector
        = { .buf = { .data = unit_file_buffer->data, .len = 0 },
            .capacity = MAX_UNIT_SIZE };

    GglError ret = fill_unit_section(*recipe_map, &concat_unit_vector, phase);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_byte_vec_append(&concat_unit_vector, GGL_STR("\n"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = fill_service_section(
        recipe_map, &concat_unit_vector, args, component_name, phase
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = fill_install_section(&concat_unit_vector, phase);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    *unit_file_buffer = concat_unit_vector.buf;
    return GGL_ERR_OK;
}
