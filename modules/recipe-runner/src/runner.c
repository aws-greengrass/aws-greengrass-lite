// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "runner.h"
#include "recipe-runner.h"
#include <errno.h>
#include <fcntl.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/constants.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/ipc/client.h>
#include <ggl/json_encode.h>
#include <ggl/json_pointer.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/recipe.h>
#include <ggl/vector.h>
#include <ggl/version.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_SCRIPT_LENGTH 10000
#define MAX_THING_NAME_LEN 128
#define MAX_RECIPE_LEN 25000

pid_t child_pid = -1; // To store child process ID

// This file is single-threaded
// NOLINTBEGIN(concurrency-mt-unsafe)

static GglError insert_config_value(int conn, int out_fd, GglBuffer json_ptr) {
    static GglBuffer key_path_mem[GGL_MAX_OBJECT_DEPTH];
    GglBufVec key_path = GGL_BUF_VEC(key_path_mem);

    GglError ret = ggl_gg_config_jsonp_parse(json_ptr, &key_path);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to parse json pointer key.");
        return ret;
    }

    static uint8_t config_value[10000];
    static uint8_t copy_config_value[10000];
    GglArena alloc = ggl_arena_init(GGL_BUF(config_value));
    GglObject result = { 0 };
    ret = ggipc_get_config_obj(conn, key_path.buf_list, NULL, &alloc, &result);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get config value for substitution.");
        return ret;
    }
    GglBuffer final_result = GGL_BUF(copy_config_value);

    if (ggl_obj_type(result) != GGL_TYPE_BUF) {
        ret = ggl_json_encode(result, &final_result);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to encode result as JSON.");
            return ret;
        }
    } else {
        final_result = ggl_obj_into_buf(result);
    }

    return ggl_file_write(out_fd, final_result);
}

static GglError split_escape_seq(
    GglBuffer escape_seq, GglBuffer *left, GglBuffer *right
) {
    for (size_t i = 0; i < escape_seq.len; i++) {
        if (escape_seq.data[i] == ':') {
            *left = ggl_buffer_substr(escape_seq, 0, i);
            *right = ggl_buffer_substr(escape_seq, i + 1, SIZE_MAX);
            return GGL_ERR_OK;
        }
    }

    GGL_LOGE("No : found in recipe escape sequence.");
    return GGL_ERR_FAILURE;
}

// TODO: Simplify this code
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GglError substitute_escape(
    int conn,
    int out_fd,
    GglBuffer escape_seq,
    GglBuffer root_path,
    GglBuffer component_name,
    GglBuffer component_version,
    GglBuffer thing_name
) {
    GglBuffer type;
    GglBuffer arg;
    GglError ret = split_escape_seq(escape_seq, &type, &arg);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_LOGT(
        "Current variable substitution: %.*s. type = %.*s; arg = %.*s",
        (int) escape_seq.len,
        escape_seq.data,
        (int) type.len,
        type.data,
        (int) arg.len,
        arg.data
    );

    if (ggl_buffer_eq(type, GGL_STR("kernel"))) {
        if (ggl_buffer_eq(arg, GGL_STR("rootPath"))) {
            return ggl_file_write(out_fd, root_path);
        }
    } else if (ggl_buffer_eq(type, GGL_STR("iot"))) {
        if (ggl_buffer_eq(arg, GGL_STR("thingName"))) {
            return ggl_file_write(out_fd, thing_name);
        }
    } else if (ggl_buffer_eq(type, GGL_STR("work"))) {
        if (ggl_buffer_eq(arg, GGL_STR("path"))) {
            ret = ggl_file_write(out_fd, root_path);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_file_write(out_fd, GGL_STR("/work/"));
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_file_write(out_fd, component_name);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            return ggl_file_write(out_fd, GGL_STR("/"));
        }
    } else if (ggl_buffer_eq(type, GGL_STR("artifacts"))) {
        if (ggl_buffer_eq(arg, GGL_STR("path"))) {
            ret = ggl_file_write(out_fd, root_path);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_file_write(out_fd, GGL_STR("/packages/"));
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_file_write(out_fd, GGL_STR("artifacts/"));
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_file_write(out_fd, component_name);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_file_write(out_fd, GGL_STR("/"));
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_file_write(out_fd, component_version);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            return ggl_file_write(out_fd, GGL_STR("/"));
        }
        if (ggl_buffer_eq(arg, GGL_STR("decompressedPath"))) {
            ret = ggl_file_write(out_fd, root_path);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_file_write(out_fd, GGL_STR("/packages/"));
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_file_write(out_fd, GGL_STR("artifacts-unarchived/"));
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_file_write(out_fd, component_name);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_file_write(out_fd, GGL_STR("/"));
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_file_write(out_fd, component_version);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            return ggl_file_write(out_fd, GGL_STR("/"));
        }
    } else if (ggl_buffer_eq(type, GGL_STR("configuration"))) {
        return insert_config_value(conn, out_fd, arg);
    }

    GGL_LOGE(
        "Unhandled variable substitution: %.*s.",
        (int) escape_seq.len,
        escape_seq.data
    );
    return GGL_ERR_FAILURE;
}

static GglError handle_escape(
    int conn,
    int out_fd,
    uint8_t **current_pointer,
    const uint8_t *end_pointer,
    GglBuffer root_path,
    GglBuffer component_name,
    GglBuffer component_version,
    GglBuffer thing_name
) {
    static uint8_t escape_contents[256];
    GglByteVec vec = GGL_BYTE_VEC(escape_contents);
    (*current_pointer)++;
    while (true) {
        if (*current_pointer == end_pointer) {
            GGL_LOGE("Recipe escape is not terminated.");
            return GGL_ERR_INVALID;
        }
        if (**current_pointer != '}') {
            GglError ret = ggl_byte_vec_push(&vec, **current_pointer);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Recipe escape exceeded max length.");
                return ret;
            }
            (*current_pointer)++;
        } else {
            (*current_pointer)++;
            return substitute_escape(
                conn,
                out_fd,
                vec.buf,
                root_path,
                component_name,
                component_version,
                thing_name
            );
        }
    }
}

static GglError process_set_env(
    int conn,
    int out_fd,
    GglMap env_values_as_map,
    GglBuffer root_path,
    GglBuffer component_name,
    GglBuffer component_version,
    GglBuffer thing_name
) {
    GGL_LOGT("Lifecycle Setenv, is a map");
    GGL_MAP_FOREACH(pair, env_values_as_map) {
        GglError ret = ggl_file_write(out_fd, GGL_STR("export "));
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        ret = ggl_file_write(out_fd, ggl_kv_key(*pair));
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        GGL_LOGT(
            "Lifecycle Setenv, map key: %.*s",
            (int) ggl_kv_key(*pair).len,
            ggl_kv_key(*pair).data
        );
        ret = ggl_file_write(out_fd, GGL_STR("="));
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        if (ggl_obj_type(*ggl_kv_val(pair)) != GGL_TYPE_BUF) {
            GGL_LOGW("Invalid lifecycle Setenv, Key values must be String");
            return GGL_ERR_INVALID;
        }
        GglBuffer val = ggl_obj_into_buf(*ggl_kv_val(pair));
        GGL_LOGT("Lifecycle Setenv, map value: %.*s", (int) val.len, val.data);
        uint8_t *current_pointer = &val.data[0];
        uint8_t *end_pointer = &val.data[val.len];
        if (val.len == 0) {
            // Add in a new line if no value is provided
            ret = ggl_file_write(out_fd, GGL_STR("\n"));
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        }
        while (true) {
            if (current_pointer == end_pointer) {
                break;
            }
            if (*current_pointer != '{') {
                ret = ggl_file_write(
                    out_fd, (GglBuffer) { current_pointer, 1 }
                );
                if (ret != GGL_ERR_OK) {
                    return ret;
                }
                current_pointer++;
            } else {
                ret = handle_escape(
                    conn,
                    out_fd,
                    &current_pointer,
                    end_pointer,
                    root_path,
                    component_name,
                    component_version,
                    thing_name
                );
                if (ret != GGL_ERR_OK) {
                    return ret;
                }
            }
        }
        ret = ggl_file_write(out_fd, GGL_STR("\n"));
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    return GGL_ERR_OK;
}

static GglError find_and_process_set_env(
    int conn,
    int out_fd,
    GglMap map_containing_setenv,
    GglBuffer root_path,
    GglBuffer component_name,
    GglBuffer component_version,
    GglBuffer thing_name
) {
    GglObject *env_values;
    GglError ret = GGL_ERR_OK;

    if (ggl_map_get(map_containing_setenv, GGL_STR("Setenv"), &env_values)) {
        if (ggl_obj_type(*env_values) != GGL_TYPE_MAP) {
            GGL_LOGE("Invalid lifecycle Setenv, Must be a map");
            return GGL_ERR_INVALID;
        }

        ret = process_set_env(
            conn,
            out_fd,
            ggl_obj_into_map(*env_values),
            root_path,
            component_name,
            component_version,
            thing_name
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

    } else {
        GGL_LOGT("No Setenv found");
    }
    return ret;
}

static GglError process_lifecycle_phase(
    int conn,
    int out_fd,
    GglMap selected_lifecycle,
    GglBuffer phase,
    GglBuffer root_path,
    GglBuffer component_name,
    GglBuffer component_version,
    GglBuffer thing_name
) {
    GglBuffer selected_script_as_buf = { 0 };
    GglMap set_env_as_map = { 0 };
    bool is_root;
    GglError ret = fetch_script_section(
        selected_lifecycle,
        phase,
        &is_root,
        &selected_script_as_buf,
        &set_env_as_map,
        NULL
    );

    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (set_env_as_map.len != 0) {
        GGL_LOGT(
            "Processing lifecycle phase Setenv for %.*s",
            (int) phase.len,
            phase.data
        );
        ret = process_set_env(
            conn,
            out_fd,
            set_env_as_map,
            root_path,
            component_name,
            component_version,
            thing_name
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to process setenv");
            return ret;
        }
    }

    if (selected_script_as_buf.len == 0) {
        // Add in a new line if no value is provided
        ret = ggl_file_write(out_fd, GGL_STR("\n"));
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    GGL_LOGT(
        "Processing lifecycle phase script for %.*s",
        (int) phase.len,
        phase.data
    );
    uint8_t *current_pointer = &selected_script_as_buf.data[0];
    uint8_t *end_pointer
        = &selected_script_as_buf.data[selected_script_as_buf.len];
    while (true) {
        if (current_pointer == end_pointer) {
            break;
        }
        if (*current_pointer != '{') {
            ret = ggl_file_write(out_fd, (GglBuffer) { current_pointer, 1 });
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            current_pointer++;
        } else {
            ret = handle_escape(
                conn,
                out_fd,
                &current_pointer,
                end_pointer,
                root_path,
                component_name,
                component_version,
                thing_name
            );
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        }
    }
    return ret;
}

static GglError write_script_with_replacement(
    int conn,
    int out_fd,
    GglMap recipe_as_map,
    GglBuffer root_path,
    GglBuffer component_name,
    GglBuffer component_version,
    GglBuffer thing_name,
    GglBuffer phase
) {
    GglMap selected_lifecycle_map = { 0 };
    GglError ret
        = select_linux_lifecycle(recipe_as_map, &selected_lifecycle_map);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to find linux Lifecycle");
        return ret;
    }

    GGL_LOGT("Processing Global Setenv");
    ret = find_and_process_set_env(
        conn,
        out_fd,
        selected_lifecycle_map,
        root_path,
        component_name,
        component_version,
        thing_name
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to process setenv");
        return ret;
    }

    GGL_LOGT(
        "Processing other Lifecycle phase: %.*s", (int) phase.len, phase.data
    );
    ret = process_lifecycle_phase(
        conn,
        out_fd,
        selected_lifecycle_map,
        phase,
        root_path,
        component_name,
        component_version,
        thing_name
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Failed to process lifecycle phase: %.*s",
            (int) phase.len,
            phase.data
        );
        return ret;
    }

    // if startup, send a ready notification before exiting
    // otherwise, simple startup scripts will fail with 'protocol' by systemd
    if (ggl_buffer_eq(GGL_STR("startup"), phase)) {
        ret = ggl_file_write(out_fd, GGL_STR("\n"));
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        ret = ggl_file_write(out_fd, GGL_STR("systemd-notify --ready\n"));
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        ret = ggl_file_write(out_fd, GGL_STR("systemd-notify --stopping\n"));
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
GglError runner(const RecipeRunnerArgs *args) {
    // Get the SocketPath from Environment Variable
    char *socket_path
        = getenv("AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT");

    if (socket_path == NULL) {
        GGL_LOGE("IPC socket path env var not set.");
        return GGL_ERR_FAILURE;
    }

    static uint8_t resp_mem[PATH_MAX];

    GglBuffer component_name = ggl_buffer_from_null_term(args->component_name);

    // Fetch the SVCUID
    GglBuffer resp = GGL_BUF(resp_mem);
    resp.len = GGL_IPC_SVCUID_STR_LEN;

    int conn = -1;
    GglError ret = ggipc_connect_by_name(
        ggl_buffer_from_null_term(socket_path), component_name, &resp, &conn
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Runner failed to authenticate with nucleus.");
        return ret;
    }

    resp.data[resp.len] = '\0';
    int sys_ret = setenv("SVCUID", (char *) resp.data, true);
    if (sys_ret != 0) {
        GGL_LOGE("setenv failed: %d.", errno);
    }
    sys_ret
        = setenv("AWS_CONTAINER_AUTHORIZATION_TOKEN", (char *) resp.data, true);
    if (sys_ret != 0) {
        GGL_LOGE("setenv failed: %d.", errno);
    }

    resp = GGL_BUF(resp_mem);
    resp.len -= 1;
    ret = ggipc_private_get_system_config(conn, GGL_STR("rootCaPath"), &resp);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get root CA path from config.");
        return ret;
    }
    resp.data[resp.len] = '\0';
    sys_ret = setenv("GG_ROOT_CA_PATH", (char *) resp.data, true);
    if (sys_ret != 0) {
        GGL_LOGE("setenv failed: %d.", errno);
    }

    resp = GGL_BUF(resp_mem);
    resp.len -= 1;
    ret = ggipc_get_config_str(
        conn,
        GGL_BUF_LIST(GGL_STR("awsRegion")),
        &GGL_STR("aws.greengrass.NucleusLite"),
        &resp
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get region from config.");
        return ret;
    }
    resp.data[resp.len] = '\0';
    sys_ret = setenv("AWS_REGION", (char *) resp.data, true);
    if (sys_ret != 0) {
        GGL_LOGE("setenv failed: %d.", errno);
    }
    sys_ret = setenv("AWS_DEFAULT_REGION", (char *) resp.data, true);
    if (sys_ret != 0) {
        GGL_LOGE("setenv failed: %d.", errno);
    }

    sys_ret = setenv("GGC_VERSION", GGL_VERSION, true);
    if (sys_ret != 0) {
        GGL_LOGE("setenv failed: %d.", errno);
    }

    resp = GGL_BUF(resp_mem);
    resp.len -= 1;
    ret = ggipc_get_config_str(
        conn,
        GGL_BUF_LIST(GGL_STR("networkProxy"), GGL_STR("proxy"), GGL_STR("url")),
        &GGL_STR("aws.greengrass.NucleusLite"),
        &resp
    );
    switch (ret) {
    case GGL_ERR_NOMEM:
        GGL_LOGE("Failed to get network proxy url from config - value longer "
                 "than supported.");
        return ret;
    case GGL_ERR_NOENTRY:
        GGL_LOGD("No network proxy set.");
        break;
    case GGL_ERR_OK: {
        resp.data[resp.len] = '\0';
        setenv("all_proxy", (char *) resp.data, true);
        setenv("ALL_PROXY", (char *) resp.data, true);
        setenv("http_proxy", (char *) resp.data, true);
        setenv("HTTP_PROXY", (char *) resp.data, true);
        setenv("https_proxy", (char *) resp.data, true);
        setenv("HTTPS_PROXY", (char *) resp.data, true);
        break;
    }
    default:
        GGL_LOGE("Failed to get proxy url from config. Error: %d.", ret);
        return ret;
    }

    resp = GGL_BUF(resp_mem);
    resp.len -= 1;
    ret = ggipc_get_config_str(
        conn,
        GGL_BUF_LIST(GGL_STR("networkProxy"), GGL_STR("noProxyAddresses")),
        &GGL_STR("aws.greengrass.NucleusLite"),
        &resp
    );
    switch (ret) {
    case GGL_ERR_NOMEM:
        GGL_LOGE("Failed to get network proxy url from config - value longer "
                 "than supported.");
        return ret;
    case GGL_ERR_NOENTRY:
        GGL_LOGD("No network proxy set.");
        break;
    case GGL_ERR_OK: {
        resp.data[resp.len] = '\0';
        setenv("no_proxy", (char *) resp.data, true);
        setenv("NO_PROXY", (char *) resp.data, true);
        break;
    }
    default:
        GGL_LOGE("Failed to get proxy url from config. Error: %d.", ret);
        return ret;
    }

    static uint8_t thing_name_mem[MAX_THING_NAME_LEN + 1];
    GglBuffer thing_name = GGL_BUF(thing_name_mem);
    thing_name.len -= 1;
    ret = ggipc_private_get_system_config(
        conn, GGL_STR("thingName"), &thing_name
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get thing name from config.");
        return ret;
    }
    thing_name.data[thing_name.len] = '\0';
    sys_ret = setenv("AWS_IOT_THING_NAME", (char *) thing_name.data, true);
    if (sys_ret != 0) {
        GGL_LOGE("setenv failed: %d.", errno);
    }

    GglBuffer root_path = GGL_BUF(resp_mem);
    ret = ggipc_private_get_system_config(
        conn, GGL_STR("rootPath"), &root_path
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get root path from config.");
        return ret;
    }

    int root_path_fd;
    ret = ggl_dir_open(root_path, O_PATH, false, &root_path_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to open rootPath.");
        return ret;
    }
    GglBuffer component_version
        = ggl_buffer_from_null_term(args->component_version);

    GglBuffer phase = ggl_buffer_from_null_term(args->phase);

    static uint8_t recipe_mem[MAX_RECIPE_LEN];
    GglArena alloc = ggl_arena_init(GGL_BUF(recipe_mem));
    GglObject recipe = { 0 };
    GGL_LOGT("Root Path: %.*s", (int) root_path.len, root_path.data);
    ret = ggl_recipe_get_from_file(
        root_path_fd, component_name, component_version, &alloc, &recipe
    );
    (void) ggl_close(root_path_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to find the recipe file");
        return ret;
    }

    // Check if TES is the dependency within the recipe
    GglObject *val;
    if (ggl_map_get(
            ggl_obj_into_map(recipe), GGL_STR("ComponentDependencies"), &val
        )) {
        if (ggl_obj_type(*val) != GGL_TYPE_MAP) {
            return GGL_ERR_PARSE;
        }
        GglObject *inner_val;
        GglMap inner_map = ggl_obj_into_map(*val);
        if (ggl_map_get(
                inner_map,
                GGL_STR("aws.greengrass.TokenExchangeService"),
                &inner_val
            )) {
            static uint8_t resp_mem2[PATH_MAX];
            GglByteVec resp_vec = GGL_BYTE_VEC(resp_mem2);
            ret = ggl_byte_vec_append(&resp_vec, GGL_STR("http://localhost:"));
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to append http://localhost:");
                return ret;
            }
            GglBuffer rest = ggl_byte_vec_remaining_capacity(resp_vec);

            ret = ggipc_get_config_str(
                conn,
                GGL_BUF_LIST(GGL_STR("port")),
                &GGL_STR("aws.greengrass.TokenExchangeService"),
                &rest
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "Failed to get port for TES server from config. Possible "
                    "reason, TES server might not have started yet."
                );
                return ret;
            }
            resp_vec.buf.len += rest.len;
            ret = ggl_byte_vec_append(
                &resp_vec, GGL_STR("/2016-11-01/credentialprovider/\0")
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to append /2016-11-01/credentialprovider/");
                return ret;
            }

            sys_ret = setenv(
                "AWS_CONTAINER_CREDENTIALS_FULL_URI",
                (char *) resp_vec.buf.data,
                true
            );
            if (sys_ret != 0) {
                GGL_LOGE(
                    "setenv AWS_CONTAINER_CREDENTIALS_FULL_URI failed: %d.",
                    errno
                );
            }
        }
    }
    int dir_fd;
    ret = ggl_dir_open(root_path, O_PATH, false, &dir_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to open %.*s.", (int) root_path.len, root_path.data);
        return ret;
    }
    int new_fd;
    ret = ggl_dir_openat(dir_fd, GGL_STR("work"), O_PATH, false, &new_fd);
    (void) ggl_close(dir_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Failed to open %.*s/work.", (int) root_path.len, root_path.data
        );
        return ret;
    }
    dir_fd = new_fd;
    ret = ggl_dir_openat(dir_fd, component_name, O_RDONLY, false, &new_fd);
    (void) ggl_close(dir_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Failed to open %.*s/work/%.*s.",
            (int) root_path.len,
            root_path.data,
            (int) component_name.len,
            component_name.data
        );
        return ret;
    }
    dir_fd = new_fd;

    sys_ret = fchdir(dir_fd);
    if (sys_ret != 0) {
        GGL_LOGE("Failed to change working directory: %d.", errno);
        return GGL_ERR_FAILURE;
    }

    int pipe_fds[2];

    sys_ret = pipe2(pipe_fds, O_CLOEXEC);
    if (sys_ret != 0) {
        GGL_LOGE("pipe failed: %d.", errno);
        return GGL_ERR_FAILURE;
    }

    pid_t pid = fork();

    if (pid < 0) {
        GGL_LOGE("Err %d when calling fork.", errno);
        return GGL_ERR_FAILURE;
    }

    // exec in parent to preserve pid
    if (pid > 0) {
        dup2(pipe_fds[0], STDIN_FILENO);
        char *argv[] = { "sh", NULL };

        execvp(argv[0], argv);
        _Exit(1);
    }

    // child
    (void) ggl_close(pipe_fds[0]);
    ret = write_script_with_replacement(
        conn,
        pipe_fds[1],
        ggl_obj_into_map(recipe),
        root_path,
        component_name,
        component_version,
        thing_name,
        phase
    );

    _Exit(ret != GGL_ERR_OK);
    return GGL_ERR_FATAL;
}

// NOLINTEND(concurrency-mt-unsafe)
