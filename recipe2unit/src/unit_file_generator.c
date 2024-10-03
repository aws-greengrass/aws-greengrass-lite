// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "unit_file_generator.h"
#include "file_operation.h"
#include "ggl/recipe2unit.h"
#include <ggl/alloc.h>
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

#define FILENAME_BUFFER_LEN 1024
#define WORKING_DIR_LEN 4096
#define MAX_SCRIPT_SIZE 10000

static const char COMPONENT_NAME[] = "recipe2unit";
static const char REQUIRES_PRIVILEGE[] = "requiresprivilege";
static const char LIFECYCLE[] = "lifecycle";
static const char ARCHITECTURE[] = "architecture";
static const char IPC_SOCKET_REL_PATH[] = "/gg-ipc.socket";

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

/// Parses [DependencyType] portion of recipe and updates the unit file
/// buffer(out) with dependency information appropriately
static GglError parse_dependency_type(
    GglKV component_dependency, GglByteVec *out
) {
    GglObject *val;
    if (component_dependency.val.type != GGL_TYPE_MAP) {
        GGL_LOGE(
            COMPONENT_NAME,
            "Any information provided under[ComponentDependencies] section "
            "only supports a key value map type."
        );
        return GGL_ERR_INVALID;
    }
    if (ggl_map_get(
            component_dependency.val.map, GGL_STR("dependencytype"), &val
        )) {
        if (val->type != GGL_TYPE_BUF) {
            return GGL_ERR_PARSE;
        }
        ggl_string_to_lower(val->buf);

        if (strncmp((char *) val->buf.data, "hard", val->buf.len) == 0) {
            GglError ret = ggl_byte_vec_append(out, GGL_STR("After=ggl."));
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
                    recipe_map, GGL_STR(LIFECYCLE), &global_lifecycle
                )) {
                if (global_lifecycle->type != GGL_TYPE_MAP) {
                    return GGL_ERR_INVALID;
                }
                if (ggl_map_get(
                        global_lifecycle->map, GGL_STR("linux"), &val
                    )) {
                    if (val->type != GGL_TYPE_MAP) {
                        GGL_LOGE(
                            COMPONENT_NAME, "Invalid Global Linux lifecycle"
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

static GglBuffer get_current_architecture(void) {
    GglBuffer current_arch = { 0 };
#if defined(__x86_64__)
    current_arch = GGL_STR("amd64");
#elif defined(__i386__)
    current_arch = GGL_STR("x86");
#elif defined(__aarch64__)
    current_arch = GGL_STR("arm");
#elif defined(__aarch64__)
    current_arch = GGL_STR("aarch64");
#endif
    return current_arch;
}

// TODO: Refactor it
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GglError manifest_selection(
    const GglMap *manifest_map,
    GglMap recipe_map,
    GglObject **selected_lifecycle_object
) {
    GglObject *platform;
    GglObject *os;
    if (ggl_map_get(*manifest_map, GGL_STR("platform"), &platform)) {
        if (platform->type != GGL_TYPE_MAP) {
            return GGL_ERR_INVALID;
        }

        // If OS is not provided then do nothing
        if (ggl_map_get(platform->map, GGL_STR("os"), &os)) {
            if (os->type != GGL_TYPE_BUF) {
                GGL_LOGE(
                    COMPONENT_NAME,
                    "Platform OS is invalid. It must be a string"
                );
                return GGL_ERR_INVALID;
            }

            GglObject *architecture_obj = { 0 };
            // fetch architecture_obj
            if (ggl_map_get(
                    platform->map, GGL_STR(ARCHITECTURE), &architecture_obj
                )) {
                if (architecture_obj->type != GGL_TYPE_BUF) {
                    GGL_LOGE(
                        COMPONENT_NAME,
                        "Platform architecture is invalid. It must be a string"
                    );
                    return GGL_ERR_INVALID;
                }
            }

            GglBuffer curr_arch = get_current_architecture();

            // Check if the current OS supported first
            if ((strncmp((char *) os->buf.data, "linux", os->buf.len) == 0
                 || strncmp((char *) os->buf.data, "*", os->buf.len) == 0)) {
                // Then check if architecture is also supported
                if (((architecture_obj == NULL)
                     || (architecture_obj->buf.len == 0)
                     || (strncmp(
                             (char *) architecture_obj->buf.data,
                             (char *) curr_arch.data,
                             architecture_obj->buf.len
                         )
                         == 0))) {
                    GglObject *selections;
                    if (ggl_map_get(
                            *manifest_map,
                            GGL_STR(LIFECYCLE),
                            selected_lifecycle_object
                        )) {
                        if ((*selected_lifecycle_object)->type
                            != GGL_TYPE_MAP) {
                            GGL_LOGE(
                                COMPONENT_NAME,
                                "Lifecycle object is not MAP type."
                            );
                            return GGL_ERR_INVALID;
                        }
                    } else if (ggl_map_get(
                                   *manifest_map,
                                   GGL_STR("selections"),
                                   &selections
                               )) {
                        if (selections->type != GGL_TYPE_LIST) {
                            return GGL_ERR_INVALID;
                        }
                        return lifecycle_selection(
                            selections, recipe_map, *selected_lifecycle_object
                        );
                    } else {
                        GGL_LOGE(
                            COMPONENT_NAME,
                            "Neither Lifecycle nor Selection data provided"
                        );
                        return GGL_ERR_INVALID;
                    }
                }

            } else {
                // If the current platform isn't linux then just proceed to
                // next and mark current cycle success
                return GGL_ERR_OK;
            }
        }
    } else {
        GGL_LOGE(COMPONENT_NAME, "Platform not provided");
        return GGL_ERR_INVALID;
    }
    return GGL_ERR_OK;
}

static GglError parse_requiresprivilege_section(
    bool *is_root, GglMap lifecycle_step
) {
    GglObject *key_object;
    if (ggl_map_get(lifecycle_step, GGL_STR(REQUIRES_PRIVILEGE), &key_object)) {
        if (key_object->type != GGL_TYPE_BUF) {
            GGL_LOGE(
                COMPONENT_NAME,
                "requiresprivilege needs to be a (true/false) value"
            );
            return GGL_ERR_INVALID;
        }
        ggl_string_to_lower(key_object->buf);

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
            GGL_LOGE(
                COMPONENT_NAME,
                "requiresprivilege needs to be a"
                "(true/false) value"
            );
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

            if (ggl_map_get(val->map, GGL_STR("script"), &key_object)) {
                if (key_object->type != GGL_TYPE_BUF) {
                    GGL_LOGE(
                        COMPONENT_NAME,
                        "script section needs to be string buffer"
                    );
                    return GGL_ERR_INVALID;
                }
                *selected_script_as_buf = key_object->buf;
            }

            if (ggl_map_get(val->map, GGL_STR("setenv"), &key_object)) {
                if (key_object->type != GGL_TYPE_MAP) {
                    GGL_LOGE(
                        COMPONENT_NAME, "set env needs to be a dictionary map"
                    );
                    return GGL_ERR_INVALID;
                }
                *set_env_as_map = key_object->map;
            }

        } else {
            GGL_LOGE(
                COMPONENT_NAME, "script section section is of invalid list type"
            );
            return GGL_ERR_INVALID;
        }
    }

    return GGL_ERR_OK;
};

static GglError concat_initial_strings(
    const GglMap *recipe_map,
    GglByteVec *script_name_prefix_vec,
    GglByteVec *working_dir_vec,
    GglByteVec *exec_start_section_vec,
    GglObject **component_name,
    Recipe2UnitArgs *args
) {
    GglError ret;
    if (!ggl_map_get(*recipe_map, GGL_STR("componentname"), component_name)) {
        return GGL_ERR_INVALID;
    }

    if ((*component_name)->type != GGL_TYPE_BUF) {
        return GGL_ERR_INVALID;
    }

    // build the script name prefix string
    ret = ggl_byte_vec_append(script_name_prefix_vec, (*component_name)->buf);
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
    ggl_byte_vec_chain_append(&ret, working_dir_vec, GGL_STR("/work"));
    // ggl_byte_vec_chain_append(&ret, working_dir_vec, (*component_name)->buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // Get the current working directory
    char cwd[WORKING_DIR_LEN];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        GGL_LOGE(COMPONENT_NAME, "Failed to get current workingdirectory");
        return GGL_ERR_FAILURE;
    }

    // build the working directory string
    ret = ggl_byte_vec_append(
        exec_start_section_vec,
        (GglBuffer) { .data = (uint8_t *) args->recipe_runner_path,
                      .len = strlen(args->recipe_runner_path) }
    );
    ggl_byte_vec_chain_append(&ret, exec_start_section_vec, GGL_STR(" -n "));
    ggl_byte_vec_chain_append(
        &ret, exec_start_section_vec, (*component_name)->buf
    );
    ggl_byte_vec_chain_append(&ret, exec_start_section_vec, GGL_STR(" -p "));
    ggl_byte_vec_chain_append(
        &ret,
        exec_start_section_vec,
        (GglBuffer) { .data = (uint8_t *) cwd, .len = strlen(cwd) }
    );
    ggl_byte_vec_chain_append(&ret, exec_start_section_vec, GGL_STR("/"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}

static GglError select_linux_manifest(
    GglMap recipe_map, GglObject *val, GglMap *selected_lifecycle_map
) {
    GglObject *selected_lifecycle_object = NULL;
    for (size_t platform_index = 0; platform_index < val->list.len;
         platform_index++) {
        if (val->list.items[platform_index].type != GGL_TYPE_MAP) {
            GGL_LOGE(
                COMPONENT_NAME,
                "Provided manifest section is in invalid format."
            );
            return GGL_ERR_INVALID;
        }
        GglError ret = manifest_selection(
            &val->list.items[platform_index].map,
            recipe_map,
            &selected_lifecycle_object
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        if (selected_lifecycle_object == NULL) {
            GGL_LOGE(COMPONENT_NAME, "No lifecycle was found for linux");
            return GGL_ERR_FAILURE;
        }
        // If a lifecycle is successfully selected then look no futher
        if (selected_lifecycle_object->type == GGL_TYPE_MAP) {
            break;
        }
    }

    if ((selected_lifecycle_object == NULL)
        || (selected_lifecycle_object->type != GGL_TYPE_MAP)) {
        GGL_LOGE(COMPONENT_NAME, "No lifecycle was found for linux");
        return GGL_ERR_FAILURE;
    }

    *selected_lifecycle_map = selected_lifecycle_object->map;

    return GGL_ERR_OK;
}

static GglError add_set_env_to_unit(GglMap set_env_as_map, GglByteVec *out) {
    for (size_t env_var_count = 0; env_var_count < set_env_as_map.len;
         env_var_count++) {
        if (set_env_as_map.pairs[env_var_count].val.type != GGL_TYPE_BUF) {
            GGL_LOGE(
                COMPONENT_NAME,
                "Invalid environment var's value, value must be a string"
            );
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
        GGL_LOGE(
            COMPONENT_NAME, "Failed to write ExecStart portion of unit files"
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
            GGL_LOGE(
                COMPONENT_NAME,
                "Failed to write setenv's environment portion to unit "
                "files"
            );
            return ret;
        }
    }
    return GGL_ERR_OK;
}

static GglError parse_install_section(
    GglMap selected_lifecycle_map,
    GglMap set_env_as_map,
    char *root_dir,
    GglMap *out_install_map,
    GglAlloc *allocator
) {
    GglObject *install_section;
    if (ggl_map_get(
            selected_lifecycle_map, GGL_STR("install"), &install_section
        )) {
        if (install_section->type != GGL_TYPE_MAP) {
            GGL_LOGE(
                COMPONENT_NAME, "install section isn't formatted correctly"
            );
            return GGL_ERR_INVALID;
        }

        GglBuffer selected_script = { 0 };
        bool is_root = false;
        GglError ret = fetch_script_section(
            selected_lifecycle_map,
            GGL_STR("install"),
            &is_root,
            &selected_script,
            &set_env_as_map
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(COMPONENT_NAME, "Cannot parse install script section");
            return GGL_ERR_FAILURE;
        }
        static uint8_t mem[PATH_MAX];
        GglByteVec socketpath_vec = GGL_BYTE_VEC(mem);
        ret = ggl_byte_vec_append(
            &socketpath_vec,
            (GglBuffer) { (uint8_t *) root_dir, strlen(root_dir) }
        );
        ggl_byte_vec_chain_append(
            &ret, &socketpath_vec, GGL_STR(IPC_SOCKET_REL_PATH)
        );

        GglObject out_object = GGL_OBJ_MAP(
            { GGL_STR(REQUIRES_PRIVILEGE), GGL_OBJ_BOOL(is_root) },
            { GGL_STR("script"), GGL_OBJ(selected_script) },
            { GGL_STR("set_env"), GGL_OBJ(set_env_as_map) },
            { GGL_STR("AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT"),
              GGL_OBJ(socketpath_vec.buf) }
        );

        ret = ggl_obj_deep_copy(&out_object, allocator);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        *out_install_map = out_object.map;

    } else {
        GGL_LOGI(COMPONENT_NAME, "No install section found within the recipe");
    }
    return GGL_ERR_OK;
}

static GglError create_standardized_install_file(
    GglByteVec script_name_prefix_vec,
    GglMap standardized_install_map,
    char *root_dir
) {
    static uint8_t json_buf[MAX_SCRIPT_SIZE];
    GglBuffer install_json_payload = GGL_BUF(json_buf);
    GglError ret = ggl_json_encode(
        GGL_OBJ(standardized_install_map), &install_json_payload
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(COMPONENT_NAME, "Failed to encode JSON.");
        return ret;
    }

    static uint8_t script_name_buf[FILENAME_BUFFER_LEN];
    GglByteVec script_name_vec = GGL_BYTE_VEC(script_name_buf);
    ret = ggl_byte_vec_append(&script_name_vec, script_name_prefix_vec.buf);
    ggl_byte_vec_chain_append(&ret, &script_name_vec, GGL_STR("install.json"));
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(COMPONENT_NAME, "Failed to append script details to vector.");
        return GGL_ERR_FAILURE;
    }

    ret = write_to_file(
        root_dir, script_name_vec.buf, install_json_payload, 0700
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(COMPONENT_NAME, "Failed to create and write the script file.");
        return ret;
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
    bool is_root = false;

    GglObject *val;
    if (ggl_map_get(recipe_map, GGL_STR("manifests"), &val)) {
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
                    GGL_STR("setenv"),
                    &selected_set_env_as_obj
                )) {
                if (selected_set_env_as_obj->type != GGL_TYPE_MAP) {
                    GGL_LOGE(
                        COMPONENT_NAME,
                        "setenv section needs to be a dictionary map type"
                    );
                    return GGL_ERR_INVALID;
                }
                set_env_as_map = selected_set_env_as_obj->map;
            } else {
                GGL_LOGI(
                    COMPONENT_NAME,
                    "setenv section not found within the linux lifecycle"
                );
            }

            static uint8_t big_buffer_for_bump[MAX_SCRIPT_SIZE + PATH_MAX];
            GglBumpAlloc the_allocator
                = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));
            GglMap standardized_install_map = { 0 };
            ret = parse_install_section(
                selected_lifecycle_map,
                set_env_as_map,
                args->root_dir,
                &standardized_install_map,
                &the_allocator.alloc
            );
            if (ret != GGL_ERR_OK) {
                return ret;
            }

            ret = create_standardized_install_file(
                script_name_prefix_vec, standardized_install_map, args->root_dir
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    COMPONENT_NAME,
                    "Failed to create standardized install file."
                );
                return ret;
            }

            //****************************************************************
            // Note: Everything below this should only deal with run or startup
            // ****************************************************************
            GglBuffer lifecycle_script_selection = { 0 };
            GglObject *startup_or_run_section;
            if (ggl_map_get(
                    selected_lifecycle_map,
                    GGL_STR("startup"),
                    &startup_or_run_section
                )) {
                if (startup_or_run_section->type == GGL_TYPE_LIST) {
                    GGL_LOGE(COMPONENT_NAME, "Startup is a list type");
                    return GGL_ERR_INVALID;
                }
                lifecycle_script_selection = GGL_STR("startup");
                ret = ggl_byte_vec_append(
                    out, GGL_STR("RemainAfterExit=true\n")
                );
                ggl_byte_vec_chain_append(&ret, out, GGL_STR("Type=oneshot\n"));
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE(
                        COMPONENT_NAME, "Failed to add unit type information"
                    );
                    return GGL_ERR_FAILURE;
                }

            } else if (ggl_map_get(
                           selected_lifecycle_map,
                           GGL_STR("run"),
                           &startup_or_run_section
                       )) {
                if (startup_or_run_section->type == GGL_TYPE_LIST) {
                    GGL_LOGE(
                        COMPONENT_NAME,
                        "'run' field in the lifecycle is of List type."
                    );
                    return GGL_ERR_INVALID;
                }
                lifecycle_script_selection = GGL_STR("run");
                ret = ggl_byte_vec_append(out, GGL_STR("Type=exec\n"));
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE(
                        COMPONENT_NAME, "Failed to add unit type information"
                    );
                    return GGL_ERR_FAILURE;
                }
            } else {
                GGL_LOGI(COMPONENT_NAME, "No startup or run provided");
                return GGL_ERR_OK;
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
                GGL_LOGE(
                    COMPONENT_NAME,
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

            ret = write_to_file(
                args->root_dir, script_name_vec.buf, selected_script, 0700
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    COMPONENT_NAME, "Failed to create and write the script file"
                );
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
                GGL_LOGE(
                    COMPONENT_NAME,
                    "Failed to write ExecStart portion of unit files"
                );
                return ret;
            }

        } else {
            GGL_LOGI(
                COMPONENT_NAME, "Invalid Manifest within the recipe file."
            );
            return GGL_ERR_INVALID;
        }
    }
    return GGL_ERR_OK;
}

static GglError fill_install_section(GglByteVec *out) {
    GglError ret = ggl_byte_vec_append(out, GGL_STR("\n[Install]\n"));
    ggl_byte_vec_chain_append(
        &ret, out, GGL_STR("WantedBy=multi-user.target\n")
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(COMPONENT_NAME, "Failed to set Install section to unit file");
        return ret;
    }

    return GGL_ERR_OK;
}

static GglError fill_service_section(
    const GglMap *recipe_map,
    GglByteVec *out,
    Recipe2UnitArgs *args,
    GglObject **component_name
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

    ret = concat_initial_strings(
        recipe_map,
        &script_name_prefix_vec,
        &working_dir_vec,
        &exec_start_section_vec,
        component_name,
        args
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGE(COMPONENT_NAME, "String Concat failed.");
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
        if (mkdir((char *) working_dir_vec.buf.data, 0700) == -1) {
            GGL_LOGE(COMPONENT_NAME, "Failed to created working directory.");
            return GGL_ERR_FAILURE;
        }
    }

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
    ggl_byte_vec_chain_append(&ret, out, GGL_STR(IPC_SOCKET_REL_PATH));
    ggl_byte_vec_chain_append(&ret, out, GGL_STR("\"\n"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = manifest_builder(
        *recipe_map, out, script_name_prefix_vec, exec_start_section_vec, args
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
    GglObject **component_name
) {
    GglByteVec concat_unit_vector
        = { .buf = { .data = unit_file_buffer->data, .len = 0 },
            .capacity = unit_file_buffer->len };

    GglError ret = fill_unit_section(*recipe_map, &concat_unit_vector);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ggl_byte_vec_append(&concat_unit_vector, GGL_STR("\n"));

    ret = fill_service_section(
        recipe_map, &concat_unit_vector, args, component_name
    );
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
