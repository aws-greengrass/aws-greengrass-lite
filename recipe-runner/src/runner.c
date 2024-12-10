// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "runner.h"
#include "recipe-runner.h"
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <ggipc/client.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/constants.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/json_pointer.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/recipe.h>
#include <ggl/vector.h>
#include <ggl/version.h>
#include <limits.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_SCRIPT_LENGTH 10000
#define MAX_THING_NAME_LEN 128
#define MAX_RECIPE_LEN 256000

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
    GglBuffer result = GGL_BUF(config_value);
    ret = ggipc_get_config_str(conn, key_path.buf_list, NULL, &result);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get config value for substitution.");
        return ret;
    }

    return ggl_file_write(out_fd, result);
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
    GglError ret = GGL_ERR_OK;
    GGL_LOGT("Lifecycle Setenv, is a map");
    GGL_MAP_FOREACH(pair, env_values_as_map) {
        ggl_file_write(out_fd, GGL_STR("export "));
        ggl_file_write(out_fd, pair->key);
        GGL_LOGT(
            "Lifecycle Setenv, map key: %.*s",
            (int) pair->key.len,
            pair->key.data
        );
        ggl_file_write(out_fd, GGL_STR("="));

        if (pair->val.type != GGL_TYPE_BUF) {
            GGL_LOGW("Invalid lifecycle Setenv, Key values must be String");
            return GGL_ERR_INVALID;
        }
        GGL_LOGD(
            "Lifecycle Setenv, map value: %.*s",
            (int) pair->val.buf.len,
            pair->val.buf.data
        );
        uint8_t *current_pointer = &pair->val.buf.data[0];
        uint8_t *end_pointer = &pair->val.buf.data[pair->val.buf.len];
        if (pair->val.buf.len == 0) {
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
    return ret;
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
        if (env_values->type != GGL_TYPE_MAP) {
            GGL_LOGE("Invalid lifecycle Setenv, Must be a map");
            return GGL_ERR_INVALID;
        }

        ret = process_set_env(
            conn,
            out_fd,
            env_values->map,
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
    ret = ggl_file_write(out_fd, GGL_STR("set -x\n"));
    if (ret != GGL_ERR_OK) {
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

    // Fetch the SVCUID
    GglBuffer resp = GGL_BUF(resp_mem);
    resp.len = GGL_IPC_MAX_SVCUID_LEN;

    int conn = -1;
    GglError ret = ggipc_connect_auth(
        ggl_buffer_from_null_term(socket_path), &resp, &conn
    );
    if (ret != GGL_ERR_OK) {
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

    // TODO: Check if TES is dependency within the recipe
    GglByteVec resp_vec = GGL_BYTE_VEC(resp_mem);
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
        GGL_LOGW("Failed to get port from config. errono: %d", ret);
    } else {
        // Only set the env var if port number is valid
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
                "setenv AWS_CONTAINER_CREDENTIALS_FULL_URI failed: %d.", errno
            );
        }
    }

    sys_ret = setenv("GGC_VERSION", GGL_VERSION, true);
    if (sys_ret != 0) {
        GGL_LOGE("setenv failed: %d.", errno);
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
    GglBuffer component_name = ggl_buffer_from_null_term(args->component_name);
    GglBuffer component_version
        = ggl_buffer_from_null_term(args->component_version);

    GglBuffer phase = ggl_buffer_from_null_term(args->phase);

    static uint8_t big_buffer_for_recipe[MAX_RECIPE_LEN];
    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_recipe));
    GglObject recipe = { 0 };
    GGL_LOGT("Root Path: %.*s", (int) root_path.len, root_path.data);
    ret = ggl_recipe_get_from_file(
        root_path_fd,
        component_name,
        component_version,
        &the_allocator.alloc,
        &recipe
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to find the recipe file");
        return ret;
    }

    int dir_fd;
    ret = ggl_dir_open(root_path, O_PATH, false, &dir_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to open %.*s.", (int) root_path.len, root_path.data);
        return ret;
    }
    ret = ggl_dir_openat(dir_fd, GGL_STR("work"), O_PATH, false, &dir_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Failed to open %.*s/work.", (int) root_path.len, root_path.data
        );
        return ret;
    }
    ret = ggl_dir_openat(dir_fd, component_name, O_RDONLY, false, &dir_fd);
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
        recipe.map,
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
