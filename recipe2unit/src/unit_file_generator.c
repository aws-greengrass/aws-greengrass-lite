// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "unit_file_generator.h"
#include "file_operation.h"
#include "ggl/recipe2unit.h"
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/recipe.h>
#include <ggl/vector.h>
#include <limits.h>
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

        if (strncmp((char *) val->buf.data, "HARD", val->buf.len) == 0) {
            GglError ret = ggl_byte_vec_append(out, GGL_STR("BindsTo=ggl."));
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_byte_vec_append(out, component_dependency.key);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_byte_vec_append(out, GGL_STR(".service\n"));
            if (ret != GGL_ERR_OK) {
                return ret;
            }

        } else {
            GglError ret = ggl_byte_vec_append(out, GGL_STR("Wants=ggl."));
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_byte_vec_append(out, component_dependency.key);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_byte_vec_append(out, GGL_STR(".service\n"));
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
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    if (ggl_map_get(recipe_map, GGL_STR("ComponentDescription"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            return GGL_ERR_PARSE;
        }

        ret = ggl_byte_vec_append(concat_unit_vector, val->buf);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        ret = ggl_byte_vec_append(concat_unit_vector, GGL_STR("\n"));
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    if (phase == RUN_STARTUP) {
        if (ggl_map_get(recipe_map, GGL_STR("ComponentDependencies"), &val)) {
            if ((val->type == GGL_TYPE_MAP) || (val->type == GGL_TYPE_LIST)) {
                return dependency_parser(val, concat_unit_vector);
            }
        }
    }

    return GGL_ERR_OK;
}

static GglError parse_requiresprivilege_section(
    bool *is_root, GglMap lifecycle_step
) {
    GglObject *key_object;
    if (ggl_map_get(
            lifecycle_step, GGL_STR("RequiresPrivilege"), &key_object
        )) {
        if (key_object->type != GGL_TYPE_BUF) {
            GGL_LOGE("RequiresPrivilege needs to be a (true/false) value");
            return GGL_ERR_INVALID;
        }

        // TODO: Check if 0 and 1 are valid
        if (strncmp((char *) key_object->buf.data, "true", key_object->buf.len)
            == 0) {
            *is_root = true;
        } else if (strncmp(
                       (char *) key_object->buf.data,
                       "false",
                       key_object->buf.len
                   )
                   == 0) {
            *is_root = false;
        } else {
            GGL_LOGE("RequiresPrivilege needs to be a"
                     "(true/false) value");
            return GGL_ERR_INVALID;
        }
    }
    return GGL_ERR_OK;
}

static GglError fetch_script_section(
    GglMap selected_lifecycle,
    GglBuffer selected_phase,
    bool *is_root,
    GglBuffer *selected_script_as_buf,
    GglMap *set_env_as_map
) {
    GglObject *val;
    if (ggl_map_get(selected_lifecycle, selected_phase, &val)) {
        if (val->type == GGL_TYPE_BUF) {
            *selected_script_as_buf = val->buf;
        } else if (val->type == GGL_TYPE_MAP) {
            GglObject *key_object;

            GglError ret = parse_requiresprivilege_section(is_root, val->map);
            if (ret != GGL_ERR_OK) {
                return ret;
            }

            if (ggl_map_get(val->map, GGL_STR("Script"), &key_object)) {
                if (key_object->type != GGL_TYPE_BUF) {
                    GGL_LOGE("Script section needs to be string buffer");
                    return GGL_ERR_INVALID;
                }
                *selected_script_as_buf = key_object->buf;
            } else {
                GGL_LOGW("Script is not in the map");
                return GGL_ERR_NOENTRY;
            }

            if (ggl_map_get(val->map, GGL_STR("Setenv"), &key_object)) {
                if (key_object->type != GGL_TYPE_MAP) {
                    GGL_LOGE("Setenv needs to be a dictionary map");
                    return GGL_ERR_INVALID;
                }
                *set_env_as_map = key_object->map;
            }

        } else {
            GGL_LOGE("Script section section is of invalid list type");
            return GGL_ERR_INVALID;
        }
    } else {
        GGL_LOGE(
            "%.*s section is not in the lifecycle",
            (int) selected_phase.len,
            selected_phase.data
        );
        return GGL_ERR_NOENTRY;
    }

    return GGL_ERR_OK;
};

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
        (GglBuffer) { .data = (uint8_t *) args->recipe_runner_path,
                      .len = strlen(args->recipe_runner_path) }
    );
    ggl_byte_vec_chain_append(&ret, exec_start_section_vec, GGL_STR(" -n "));
    ggl_byte_vec_chain_append(
        &ret, exec_start_section_vec, (*component_name)->buf
    );
    ggl_byte_vec_chain_append(&ret, exec_start_section_vec, GGL_STR(" -v "));
    ggl_byte_vec_chain_append(&ret, exec_start_section_vec, component_version);
    ggl_byte_vec_chain_append(&ret, exec_start_section_vec, GGL_STR(" -p "));

    GglBuffer cwd = ggl_byte_vec_remaining_capacity(*exec_start_section_vec);
    // Get the current working directory
    if (getcwd((char *) cwd.data, cwd.len) == NULL) {
        GGL_LOGE("Failed to get current working directory.");
        return GGL_ERR_FAILURE;
    }
    cwd.len = strlen((char *) cwd.data);
    exec_start_section_vec->buf.len += cwd.len;

    ggl_byte_vec_chain_append(&ret, exec_start_section_vec, GGL_STR("/"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}

static GglError add_set_env_to_unit(GglMap set_env_as_map, GglByteVec *out) {
    for (size_t env_var_count = 0; env_var_count < set_env_as_map.len;
         env_var_count++) {
        if (set_env_as_map.pairs[env_var_count].val.type != GGL_TYPE_BUF) {
            GGL_LOGE("Invalid environment var's value, value must be a string");
            return GGL_ERR_INVALID;
        }

        GglError ret = ggl_byte_vec_append(out, GGL_STR("Environment=\""));
        ggl_byte_vec_chain_append(
            &ret, out, set_env_as_map.pairs[env_var_count].key
        );
        ggl_byte_vec_chain_append(&ret, out, GGL_STR("="));
        ggl_byte_vec_chain_append(
            &ret, out, set_env_as_map.pairs[env_var_count].val.buf
        );
        ggl_byte_vec_chain_append(&ret, out, GGL_STR("\"\n"));
    }
    return GGL_ERR_OK;
}

static GglError update_unit_file_buffer(
    GglByteVec *out,
    GglByteVec exec_start_section_vec,
    GglByteVec script_name_vec,
    char *arg_user,
    char *arg_group,
    bool is_root,
    GglMap set_env_as_map
) {
    GglError ret = ggl_byte_vec_append(out, GGL_STR("ExecStart="));
    ggl_byte_vec_chain_append(&ret, out, exec_start_section_vec.buf);
    ggl_byte_vec_chain_append(&ret, out, script_name_vec.buf);
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("\n"));
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to write ExecStart portion of unit files");
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
            &ret,
            out,
            (GglBuffer) { .data = (uint8_t *) arg_user,
                          .len = strlen(arg_user) }
        );
        ggl_byte_vec_chain_append(&ret, out, GGL_STR("\nGroup="));
        ggl_byte_vec_chain_append(
            &ret,
            out,
            (GglBuffer) { .data = (uint8_t *) arg_group,
                          .len = strlen(arg_group) }
        );
        ggl_byte_vec_chain_append(&ret, out, GGL_STR("\n"));
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    if (set_env_as_map.len != 0) {
        ret = add_set_env_to_unit(set_env_as_map, out);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to write setenv's environment portion to unit "
                     "files");
            return ret;
        }
    }
    return GGL_ERR_OK;
}

// TODO: Refactor it
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GglError manifest_builder(
    GglMap recipe_map,
    GglByteVec *out,
    GglByteVec script_name_prefix_vec,
    GglByteVec exec_start_section_vec,
    Recipe2UnitArgs *args,
    PhaseSelection current_phase
) {
    bool is_root = false;

    GglObject *val;
    if (ggl_map_get(recipe_map, GGL_STR("Manifests"), &val)) {
        if (val->type == GGL_TYPE_LIST) {
            GglMap selected_lifecycle_map = { 0 };

            GglError ret = select_linux_manifest(
                recipe_map, val, &selected_lifecycle_map
            );
            if (ret != GGL_ERR_OK) {
                return ret;
            }

            GglObject *selected_set_env_as_obj = { 0 };
            GglMap set_env_as_map = { 0 };
            if (ggl_map_get(
                    selected_lifecycle_map,
                    GGL_STR("Setenv"),
                    &selected_set_env_as_obj
                )) {
                if (selected_set_env_as_obj->type != GGL_TYPE_MAP) {
                    GGL_LOGE("Setenv section needs to be a dictionary map type"
                    );
                    return GGL_ERR_INVALID;
                }
                set_env_as_map = selected_set_env_as_obj->map;
            } else {
                GGL_LOGI("Setenv section not found within the linux lifecycle");
            }

            //****************************************************************
            // Note: Everything below this should only deal with run or startup
            // ****************************************************************
            GglBuffer lifecycle_script_selection = { 0 };
            GglObject *startup_or_run_section;

            if (current_phase == INSTALL) {
                lifecycle_script_selection = GGL_STR("install");
                ggl_byte_vec_chain_append(&ret, out, GGL_STR("Type=oneshot\n"));
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE("Failed to add unit type information");
                    return GGL_ERR_FAILURE;
                }
            } else if (current_phase == RUN_STARTUP) {
                if (ggl_map_get(
                        selected_lifecycle_map,
                        GGL_STR("startup"),
                        &startup_or_run_section
                    )) {
                    if (startup_or_run_section->type == GGL_TYPE_LIST) {
                        GGL_LOGE("Startup is a list type");
                        return GGL_ERR_INVALID;
                    }
                    lifecycle_script_selection = GGL_STR("startup");
                    ret = ggl_byte_vec_append(
                        out, GGL_STR("RemainAfterExit=true\n")
                    );
                    ggl_byte_vec_chain_append(
                        &ret, out, GGL_STR("Type=oneshot\n")
                    );
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
                        GGL_LOGE("'run' field in the lifecycle is of List type."
                        );
                        return GGL_ERR_INVALID;
                    }
                    lifecycle_script_selection = GGL_STR("run");
                    ret = ggl_byte_vec_append(out, GGL_STR("Type=exec\n"));
                    if (ret != GGL_ERR_OK) {
                        GGL_LOGE("Failed to add unit type information");
                        return GGL_ERR_FAILURE;
                    }
                } else {
                    GGL_LOGI("No startup or run provided");
                    return GGL_ERR_OK;
                }
            }

            GglBuffer selected_script = { 0 };
            ret = fetch_script_section(
                selected_lifecycle_map,
                lifecycle_script_selection,
                &is_root,
                &selected_script,
                &set_env_as_map
            );
            if (ret != GGL_ERR_OK) {
                return ret;
            }

            static uint8_t script_name_buf[PATH_MAX - 1];
            GglByteVec script_name_vec = GGL_BYTE_VEC(script_name_buf);
            ret = ggl_byte_vec_append(
                &script_name_vec, script_name_prefix_vec.buf
            );
            ggl_byte_vec_chain_append(
                &ret, &script_name_vec, lifecycle_script_selection
            );
            if (ret != GGL_ERR_OK) {
                return GGL_ERR_FAILURE;
            }
            ret = write_to_file(
                args->root_dir, script_name_vec.buf, selected_script, 0755
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to create and write the script file");
                return ret;
            }

            ret = update_unit_file_buffer(
                out,
                exec_start_section_vec,
                script_name_vec,
                args->user,
                args->group,
                is_root,
                set_env_as_map
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to write ExecStart portion of unit files");
                return ret;
            }

        } else {
            GGL_LOGI("Invalid Manifest within the recipe file.");
            return GGL_ERR_INVALID;
        }
    }
    return GGL_ERR_OK;
}

static GglError fill_install_section(
    GglByteVec *out, PhaseSelection current_phase
) {
    if (current_phase != INSTALL) {
        GglError ret = ggl_byte_vec_append(out, GGL_STR("\n[Install]\n"));
        ggl_byte_vec_chain_append(
            &ret, out, GGL_STR("WantedBy=multi-user.target\n")
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

    // TODO: Working directory needs ownership changed
    // ret = ggl_byte_vec_append(out, GGL_STR("WorkingDirectory="));
    // ggl_byte_vec_chain_append(&ret, out, working_dir_vec.buf);
    // ggl_byte_vec_chain_append(&ret, out, GGL_STR("\n"));
    // if (ret != GGL_ERR_OK) {
    //     return ret;
    // }

    // Create the working directory if not existant
    int working_dir;
    ret = ggl_dir_open(working_dir_vec.buf, O_PATH, true, &working_dir);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to created working directory.");
        return ret;
    }
    GGL_CLEANUP(cleanup_close, working_dir);

    // Add Env Var for GG_root path
    ret = ggl_byte_vec_append(
        out,
        GGL_STR(
            "Environment=\"AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT="
        )
    );
    ggl_byte_vec_chain_append(
        &ret,
        out,
        (GglBuffer) { (uint8_t *) args->root_dir, strlen(args->root_dir) }
    );
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("/gg-ipc.socket"));
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("\"\n"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = manifest_builder(
        *recipe_map,
        out,
        script_name_prefix_vec,
        exec_start_section_vec,
        args,
        phase
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
