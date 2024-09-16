// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "unit_file_generator.h"
#include "file_operation.h"
#include "ggl/recipe2unit.h"
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

#define FILENAME_BUFFER_LEN 1024
#define WORKING_DIR_LEN 4096

static void ggl_string_to_lower(GglBuffer object_object_to_lower) {
    for (size_t key_count = 0; key_count < object_object_to_lower.len;
         key_count++) {
        if (object_object_to_lower.data[key_count] >= 'A'
            && object_object_to_lower.data[key_count] <= 'Z') {
            object_object_to_lower.data[key_count]
                = object_object_to_lower.data[key_count] + ('a' - 'A');
        }
    }
}

// TODO: Refactor it
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GglError dependency_parser(GglObject *dependency_obj, GglByteVec *out) {
    if (dependency_obj->type != GGL_TYPE_MAP) {
        return GGL_ERR_INVALID;
    }
    for (size_t count = 0; count < dependency_obj->map.len; count++) {
        if (dependency_obj->map.pairs[count].val.type == GGL_TYPE_MAP) {
            GglObject *val;
            if (ggl_map_get(
                    dependency_obj->map.pairs[count].val.map,
                    GGL_STR("dependencytype"),
                    &val
                )) {
                if (val->type != GGL_TYPE_BUF) {
                    return GGL_ERR_PARSE;
                }
                ggl_string_to_lower(val->buf);

                if (strncmp((char *) val->buf.data, "hard", val->buf.len)
                    == 0) {
                    GglError ret
                        = ggl_byte_vec_append(out, GGL_STR("After=ggl."));
                    if (ret != GGL_ERR_OK) {
                        return ret;
                    }
                    ret = ggl_byte_vec_append(
                        out, dependency_obj->map.pairs[count].key
                    );
                    if (ret != GGL_ERR_OK) {
                        return ret;
                    }
                    ret = ggl_byte_vec_append(out, GGL_STR(".service\n"));
                    if (ret != GGL_ERR_OK) {
                        return ret;
                    }
                } else {
                    GglError ret
                        = ggl_byte_vec_append(out, GGL_STR("Wants=ggl."));
                    if (ret != GGL_ERR_OK) {
                        return ret;
                    }
                    ret = ggl_byte_vec_append(
                        out, dependency_obj->map.pairs[count].key
                    );
                    if (ret != GGL_ERR_OK) {
                        return ret;
                    }
                    ret = ggl_byte_vec_append(out, GGL_STR(".service\n"));
                    if (ret != GGL_ERR_OK) {
                        return ret;
                    }
                }
            }
        }
        // TODO: deal with version, look conflictsWith
    }

    return GGL_ERR_OK;
}

static GglError fill_unit_section(
    GglMap recipe_map, GglByteVec *concat_unit_vector
) {
    GglObject *val;

    GglError ret = ggl_byte_vec_append(concat_unit_vector, GGL_STR("[Unit]\n"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_byte_vec_append(concat_unit_vector, GGL_STR("Description="));
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    if (ggl_map_get(recipe_map, GGL_STR("componentdescription"), &val)) {
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

    if (ggl_map_get(recipe_map, GGL_STR("componentdependencies"), &val)) {
        if ((val->type == GGL_TYPE_MAP) || (val->type == GGL_TYPE_LIST)) {
            return dependency_parser(val, concat_unit_vector);
        }
    }

    return GGL_ERR_OK;
}

static GglError lifecycle_selection(
    GglObject *selection_obj,
    GglMap recipe_map,
    GglObject *selected_lifecycle_object
) {
    GglObject *val;
    for (size_t selection_index = 0; selection_index < selection_obj->list.len;
         selection_index++) {
        if ((strncmp(
                 (char *) selection_obj->list.items[selection_index].buf.data,
                 "all",
                 selection_obj->list.items[selection_index].buf.len
             )
             == 0)
            || (strncmp(
                    (char *) selection_obj->list.items[selection_index]
                        .buf.data,
                    "linux",
                    selection_obj->list.items[selection_index].buf.len
                )
                == 0)) {
            GglObject *global_lifecycle;
            // Fetch the global Lifecycle object and match the
            // name with the first occurrence of selection
            if (ggl_map_get(
                    recipe_map, GGL_STR("lifecycle"), &global_lifecycle
                )) {
                if (global_lifecycle->type != GGL_TYPE_MAP) {
                    return GGL_ERR_INVALID;
                }
                if (ggl_map_get(
                        global_lifecycle->map, GGL_STR("linux"), &val
                    )) {
                    if (val->type != GGL_TYPE_MAP) {
                        GGL_LOGE(
                            "recipe2unit", "Invalid Global Linux lifecycle"
                        );
                        return GGL_ERR_INVALID;
                    }
                    *selected_lifecycle_object = *val;
                }
            }
        }
    }
    return GGL_ERR_OK;
}

// TODO: Refactor it
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GglError manifest_selection(
    GglMap manifest_map, GglMap recipe_map, GglObject *selected_lifecycle_object
) {
    GglObject *val;
    if (ggl_map_get(manifest_map, GGL_STR("platform"), &val)) {
        if (val->type == GGL_TYPE_MAP) {
            // If OS is not provided then do nothing
            if (ggl_map_get(val->map, GGL_STR("os"), &val)) {
                if (val->type != GGL_TYPE_BUF) {
                    GGL_LOGE("recipe2unit", "Platform OS invalid input");
                    return GGL_ERR_INVALID;
                }
                if (strncmp((char *) val->buf.data, "linux", val->buf.len) == 0
                    || strncmp((char *) val->buf.data, "*", val->buf.len)
                        == 0) {
                    if (ggl_map_get(manifest_map, GGL_STR("lifecycle"), &val)) {
                        if (val->type != GGL_TYPE_MAP) {
                            return GGL_ERR_INVALID;
                        }
                        // if linux lifecycle is found return the object
                        *selected_lifecycle_object = *val;

                    } else if (ggl_map_get(
                                   val->map, GGL_STR("selections"), &val
                               )) {
                        if (val->type != GGL_TYPE_LIST) {
                            return GGL_ERR_INVALID;
                        }
                        return lifecycle_selection(
                            val, recipe_map, selected_lifecycle_object
                        );
                    } else {
                        GGL_LOGE(
                            "recipe2unit",
                            "Neither Lifecycle or Selection data provided"
                        );
                        return GGL_ERR_INVALID;
                    }
                } else {
                    // If the current platform isn't linux then just proceed to
                    // next and mark current cycle success
                    return GGL_ERR_OK;
                }
            }
        } else {
            return GGL_ERR_INVALID;
        }
    } else {
        GGL_LOGE("recipe2unit", "Platform not provided");
        return GGL_ERR_INVALID;
    }
    return GGL_ERR_OK;
}

static GglError fetch_script_section(
    GglMap selected_lifecycle,
    GglBuffer selected_phase,
    bool *is_root,
    GglObject *selected_script
) {
    (void) selected_lifecycle;
    (void) selected_phase;

    GglObject *val;

    if (ggl_map_get(selected_lifecycle, selected_phase, &val)) {
        if (val->type == GGL_TYPE_BUF) {
            *selected_script = *val;
        } else if (val->type == GGL_TYPE_MAP) {
            GglObject *key_object;

            if (ggl_map_get(
                    val->map, GGL_STR("requiresprivilege"), &key_object
                )) {
                if (key_object->type != GGL_TYPE_BUF) {
                    GGL_LOGE(
                        "recipe2unit",
                        "requiresprivilege needs to be a boolean value"
                    );
                    return GGL_ERR_INVALID;
                }
                ggl_string_to_lower(key_object->buf);

                // TODO: Check if 0 and 1 are valid
                if (strncmp(
                        (char *) key_object->buf.data,
                        "true",
                        key_object->buf.len
                    )
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
                    GGL_LOGE(
                        "recipe2unit",
                        "requiresprivilege needs to be a boolean value "
                        "(true/false)"
                    );
                    return GGL_ERR_INVALID;
                }
            }
            if (ggl_map_get(val->map, GGL_STR("script"), &key_object)) {
                if (key_object->type != GGL_TYPE_BUF) {
                    GGL_LOGE(
                        "recipe2unit",
                        "script section needs to be string buffer"
                    );
                    return GGL_ERR_INVALID;
                }
                *selected_script = *key_object;
            }
        } else {
            GGL_LOGE(
                "recipe2unit", "script section section is of invalid list type"
            );
            return GGL_ERR_INVALID;
        }
    }

    return GGL_ERR_OK;
};

static GglError concat_inital_strings(
    GglMap recipe_map,
    GglByteVec *script_name_prefix_vec,
    GglByteVec *working_dir_vec,
    GglByteVec *exec_start_section_vec,
    Recipe2UnitArgs *args
) {
    GglError ret;
    GglObject *val;
    if (ggl_map_get(recipe_map, GGL_STR("componentname"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            return GGL_ERR_INVALID;
        }

        // build the script name prefix string
        ret = ggl_byte_vec_append(script_name_prefix_vec, val->buf);
        ggl_byte_vec_chain_append(
            &ret, script_name_prefix_vec, GGL_STR(".script.")
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        // build the working directory string
        ret = ggl_byte_vec_append(
            working_dir_vec,
            (GglBuffer) { .data = (uint8_t *) args->root_dir,
                          .len = strlen(args->root_dir) }
        );
        ggl_byte_vec_chain_append(&ret, working_dir_vec, GGL_STR("/work/"));
        ggl_byte_vec_chain_append(&ret, working_dir_vec, val->buf);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        // Get the current working directory
        char cwd[WORKING_DIR_LEN];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            GGL_LOGE("recipe2unit", "Failed to get current workingdirectory");
            return GGL_ERR_FAILURE;
        }

        // build the working directory string
        ret = ggl_byte_vec_append(
            exec_start_section_vec,
            (GglBuffer) { .data = (uint8_t *) args->recipe_runner_path,
                          .len = strlen(args->recipe_runner_path) }
        );
        ggl_byte_vec_chain_append(
            &ret, exec_start_section_vec, GGL_STR(" -n ")
        );
        ggl_byte_vec_chain_append(&ret, exec_start_section_vec, val->buf);
        ggl_byte_vec_chain_append(
            &ret, exec_start_section_vec, GGL_STR(" -p ")
        );
        ggl_byte_vec_chain_append(
            &ret,
            exec_start_section_vec,
            (GglBuffer) { .data = (uint8_t *) cwd, .len = strlen(cwd) }
        );
        ggl_byte_vec_chain_append(&ret, exec_start_section_vec, GGL_STR("/"));
        if (ret != GGL_ERR_OK) {
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
    Recipe2UnitArgs *args
) {
    GglObject *val;
    GglObject selected_script = { 0 };
    GglObject selected_lifecycle_object = { 0 };
    bool is_root = false;

    if (ggl_map_get(recipe_map, GGL_STR("manifests"), &val)) {
        if (val->type == GGL_TYPE_LIST) {
            for (size_t platform_index = 0; platform_index < val->list.len;
                 platform_index++) {
                if (val->list.items[platform_index].type != GGL_TYPE_MAP) {
                    GGL_LOGE(
                        "recipe2unit",
                        "Provided manifest section is in invalid format."
                    );
                    return GGL_ERR_INVALID;
                }
                GglError ret = manifest_selection(
                    val->list.items[platform_index].map,
                    recipe_map,
                    &selected_lifecycle_object
                );
                if (ret != GGL_ERR_OK) {
                    return ret;
                }
                // If a lifecycle is successfully selected then look no futher
                if (selected_lifecycle_object.type == GGL_TYPE_MAP) {
                    break;
                }
            }

            if (selected_lifecycle_object.type != GGL_TYPE_MAP) {
                GGL_LOGE("recipe2unit", "No lifecycle was found for linux");
                return GGL_ERR_FAILURE;
            }

            //****************************************************************
            // Note: Everything below this should only deal with run or startup
            // ****************************************************************
            GglBuffer lifecycle_script_selection = { 0 };
            GglObject *selection_made;
            if (ggl_map_get(
                    selected_lifecycle_object.map,
                    GGL_STR("startup"),
                    &selection_made
                )) {
                if (selection_made->type == GGL_TYPE_LIST) {
                    GGL_LOGE("recipe2unit", "Startup is a list type");
                    return GGL_ERR_INVALID;
                }
                lifecycle_script_selection = GGL_STR("startup");
                GglError ret = ggl_byte_vec_append(
                    out, GGL_STR("RemainAfterExit=true\n")
                );
                ggl_byte_vec_chain_append(&ret, out, GGL_STR("Type=oneshot\n"));
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE(
                        "recipe2unit", "Failed to add unit type information"
                    );
                    return GGL_ERR_FAILURE;
                }

            } else if (ggl_map_get(
                           selected_lifecycle_object.map,
                           GGL_STR("run"),
                           &selection_made
                       )) {
                if (selection_made->type != GGL_TYPE_MAP) {
                    return GGL_ERR_INVALID;
                }
                lifecycle_script_selection = GGL_STR("run");
                GglError ret = ggl_byte_vec_append(out, GGL_STR("Type=exec\n"));
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE(
                        "recipe2unit", "Failed to add unit type information"
                    );
                    return GGL_ERR_FAILURE;
                }
            } else {
                GGL_LOGI("recipe2unit", "No startup or run provided");
                return GGL_ERR_OK;
            }

            GglError ret = fetch_script_section(
                selected_lifecycle_object.map,
                lifecycle_script_selection,
                &is_root,
                &selected_script
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "recipe2unit",
                    "Cannot parse %.*s script section",
                    (int) lifecycle_script_selection.len,
                    lifecycle_script_selection.data
                );
                return GGL_ERR_FAILURE;
            }

            static uint8_t script_name_buf[FILENAME_BUFFER_LEN];
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

            ret = write_to_file_executable(
                args->root_dir, script_name_vec.buf, selected_script.buf
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "recipe2unit", "Failed to create and write the script file"
                );
                return ret;
            }

            ggl_byte_vec_chain_append(&ret, out, GGL_STR("ExecStart="));
            ggl_byte_vec_chain_append(&ret, out, exec_start_section_vec.buf);
            ggl_byte_vec_chain_append(&ret, out, script_name_vec.buf);
            ggl_byte_vec_chain_append(&ret, out, GGL_STR("\n"));
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "recipe2unit",
                    "Failed to write ExecStart portion of unit files"
                );
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
                    (GglBuffer) { .data = (uint8_t *) args->user,
                                  .len = strlen(args->user) }
                );
                ggl_byte_vec_chain_append(&ret, out, GGL_STR("\nGroup="));
                ggl_byte_vec_chain_append(
                    &ret,
                    out,
                    (GglBuffer) { .data = (uint8_t *) args->group,
                                  .len = strlen(args->group) }
                );
                ggl_byte_vec_chain_append(&ret, out, GGL_STR("\n"));
                if (ret != GGL_ERR_OK) {
                    return ret;
                }
            }

        } else {
            GGL_LOGI("recipe2unit", "Invalid Manifest within the recipe file.");
            return GGL_ERR_INVALID;
        }
    }
    return GGL_ERR_OK;
}

static GglError fill_environment_variables(
    GglByteVec *out, Recipe2UnitArgs *args
) {
    GglError ret = ggl_byte_vec_append(
        out, GGL_STR("Environment=\"AWS_IOT_THING_NAME=")
    );
    ggl_byte_vec_chain_append(
        &ret,
        out,
        (GglBuffer) { (uint8_t *) args->thing_name, strlen(args->thing_name) }
    );
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("\"\n"));

    ggl_byte_vec_chain_append(&ret, out, GGL_STR("Environment=\"AWS_REGION="));
    ggl_byte_vec_chain_append(
        &ret,
        out,
        (GglBuffer) { (uint8_t *) args->aws_region, strlen(args->aws_region) }
    );
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("\"\n"));

    ggl_byte_vec_chain_append(
        &ret, out, GGL_STR("Environment=\"AWS_DEFAULT_REGION=")
    );
    ggl_byte_vec_chain_append(
        &ret,
        out,
        (GglBuffer) { (uint8_t *) args->aws_region, strlen(args->aws_region) }
    );
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("\"\n"));

    ggl_byte_vec_chain_append(&ret, out, GGL_STR("Environment=\"GGC_VERSION="));
    ggl_byte_vec_chain_append(
        &ret,
        out,
        (GglBuffer) { (uint8_t *) args->ggc_version, strlen(args->ggc_version) }
    );
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("\"\n"));

    ggl_byte_vec_chain_append(
        &ret, out, GGL_STR("Environment=\"GG_ROOT_CA_PATH=")
    );
    ggl_byte_vec_chain_append(
        &ret,
        out,
        (GglBuffer) { (uint8_t *) args->gg_root_ca_path,
                      strlen(args->gg_root_ca_path) }
    );
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("\"\n"));

    ggl_byte_vec_chain_append(
        &ret,
        out,
        GGL_STR(
            "Environment=\"AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT="
        )
    );
    ggl_byte_vec_chain_append(
        &ret,
        out,
        (GglBuffer) { (uint8_t *) args->socket_path, strlen(args->socket_path) }
    );
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("\"\n"));

    ggl_byte_vec_chain_append(
        &ret, out, GGL_STR("Environment=\"AWS_CONTAINER_AUTHORIZATION_TOKEN=")
    );
    ggl_byte_vec_chain_append(
        &ret,
        out,
        (GglBuffer) { (uint8_t *) args->aws_container_auth_token,
                      strlen(args->aws_container_auth_token) }
    );
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("\"\n"));

    ggl_byte_vec_chain_append(
        &ret, out, GGL_STR("Environment=\"AWS_CONTAINER_CREDENTIALS_FULL_URI=")
    );
    ggl_byte_vec_chain_append(
        &ret,
        out,
        (GglBuffer) { (uint8_t *) args->aws_container_cred_url,
                      strlen(args->aws_container_cred_url) }
    );
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("\"\n"));

    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "recipe2unit", "Failed to set environment variables to unit file"
        );
        return ret;
    }
    return GGL_ERR_OK;
}

static GglError fill_install_section(GglByteVec *out) {
    GglError ret = ggl_byte_vec_append(out, GGL_STR("\n[Install]\n"));
    ggl_byte_vec_chain_append(
        &ret, out, GGL_STR("WantedBy=GreengrassCore.target\n")
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("recipe2unit", "Failed to set Install section to unit file");
        return ret;
    }

    return GGL_ERR_OK;
}

static GglError fill_service_section(
    GglMap recipe_map, GglByteVec *out, Recipe2UnitArgs *args
) {
    GglError ret = ggl_byte_vec_append(out, GGL_STR("[Service]\n"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t working_dir_buf[WORKING_DIR_LEN];
    GglByteVec working_dir_vec = GGL_BYTE_VEC(working_dir_buf);

    static uint8_t exec_start_section_buf[2 * WORKING_DIR_LEN];
    GglByteVec exec_start_section_vec = GGL_BYTE_VEC(exec_start_section_buf);

    static uint8_t script_name_prefix_buf[FILENAME_BUFFER_LEN];
    GglByteVec script_name_prefix_vec = GGL_BYTE_VEC(script_name_prefix_buf);
    ret = ggl_byte_vec_append(&script_name_prefix_vec, GGL_STR("ggl."));

    ret = concat_inital_strings(
        recipe_map,
        &script_name_prefix_vec,
        &working_dir_vec,
        &exec_start_section_vec,
        args
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGE("recipe2unit", "String Concat failed.");
        return ret;
    }

    ret = ggl_byte_vec_append(out, GGL_STR("WorkingDirectory="));
    ggl_byte_vec_chain_append(&ret, out, working_dir_vec.buf);
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("\n"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // Create the working directory if not existant
    struct stat st = { 0 };
    if (stat((char *) working_dir_vec.buf.data, &st) == -1) {
        mkdir((char *) working_dir_vec.buf.data, 0700);
    }

    ret = manifest_builder(
        recipe_map, out, script_name_prefix_vec, exec_start_section_vec, args
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}

GglError generate_systemd_unit(
    GglMap recipe_map, GglBuffer *unit_file_buffer, Recipe2UnitArgs *args
) {
    GglByteVec concat_unit_vector
        = { .buf = { .data = unit_file_buffer->data, .len = 0 },
            .capacity = unit_file_buffer->len };

    GglError ret = fill_unit_section(recipe_map, &concat_unit_vector);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ggl_byte_vec_append(&concat_unit_vector, GGL_STR("\n"));

    ret = fill_service_section(recipe_map, &concat_unit_vector, args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = fill_environment_variables(&concat_unit_vector, args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = fill_install_section(&concat_unit_vector);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    *unit_file_buffer = concat_unit_vector.buf;
    return GGL_ERR_OK;
}
