// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "runner.h"
#include <errno.h>
#include <fcntl.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/eventstream/decode.h>
#include <gg/eventstream/types.h>
#include <gg/file.h>
#include <gg/flags.h>
#include <gg/ipc/client.h>
#include <gg/ipc/client_priv.h>
#include <gg/ipc/client_raw.h>
#include <gg/ipc/limits.h>
#include <gg/json_encode.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/json_pointer.h>
#include <ggl/nucleus/constants.h>
#include <ggl/recipe.h>
#include <limits.h>
#include <recipe-runner.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_SCRIPT_LENGTH 10000
#define MAX_THING_NAME_LEN 128

pid_t child_pid = -1; // To store child process ID

static GgError write_escaped_char(int out_fd, uint8_t c) {
    if (c == '"' || c == '\\' || c == '$' || c == '`') {
        GgError ret = gg_file_write(out_fd, GG_STR("\\"));
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }
    return gg_file_write(out_fd, (GgBuffer) { &c, 1 });
}

static GgError write_escaped_value(int out_fd, GgBuffer value) {
    for (size_t i = 0; i < value.len; i++) {
        GgError ret = write_escaped_char(out_fd, value.data[i]);
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    return GG_ERR_OK;
}

static GgError insert_config_value(int out_fd, GgBuffer json_ptr) {
    static GgBuffer key_path_mem[GG_MAX_OBJECT_DEPTH];
    GgBufVec key_path = GG_BUF_VEC(key_path_mem);

    GgError ret = ggl_gg_config_jsonp_parse(json_ptr, &key_path);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to parse json pointer key.");
        return ret;
    }

    static uint8_t config_value[10000];
    static uint8_t copy_config_value[10000];
    GgObject result = { 0 };
    ret = ggipc_get_config(
        key_path.buf_list, NULL, GG_BUF(config_value), &result
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get config value for substitution.");
        return ret;
    }
    GgBuffer final_result = GG_BUF(copy_config_value);

    if (gg_obj_type(result) != GG_TYPE_BUF) {
        GgByteVec vec = gg_byte_vec_init(final_result);
        ret = gg_json_encode(result, gg_byte_vec_writer(&vec));
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to encode result as JSON.");
            return ret;
        }
        final_result = vec.buf;
    } else {
        final_result = gg_obj_into_buf(result);
    }

    return write_escaped_value(out_fd, final_result);
}

static GgError split_escape_seq(
    GgBuffer escape_seq, GgBuffer *left, GgBuffer *right
) {
    for (size_t i = 0; i < escape_seq.len; i++) {
        if (escape_seq.data[i] == ':') {
            *left = gg_buffer_substr(escape_seq, 0, i);
            *right = gg_buffer_substr(escape_seq, i + 1, SIZE_MAX);
            return GG_ERR_OK;
        }
    }

    GG_LOGE("No : found in recipe escape sequence.");
    return GG_ERR_FAILURE;
}

// TODO: Simplify this code
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GgError substitute_escape(
    int out_fd,
    GgBuffer escape_seq,
    GgBuffer root_path,
    GgBuffer component_name,
    GgBuffer component_version,
    GgBuffer thing_name
) {
    GgBuffer type;
    GgBuffer arg;
    GgError ret = split_escape_seq(escape_seq, &type, &arg);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GG_LOGT(
        "Current variable substitution: %.*s. type = %.*s; arg = %.*s",
        (int) escape_seq.len,
        escape_seq.data,
        (int) type.len,
        type.data,
        (int) arg.len,
        arg.data
    );

    if (gg_buffer_eq(type, GG_STR("kernel"))) {
        if (gg_buffer_eq(arg, GG_STR("rootPath"))) {
            return gg_file_write(out_fd, root_path);
        }
    } else if (gg_buffer_eq(type, GG_STR("iot"))) {
        if (gg_buffer_eq(arg, GG_STR("thingName"))) {
            return gg_file_write(out_fd, thing_name);
        }
    } else if (gg_buffer_eq(type, GG_STR("work"))) {
        if (gg_buffer_eq(arg, GG_STR("path"))) {
            ret = gg_file_write(out_fd, root_path);
            if (ret != GG_ERR_OK) {
                return ret;
            }
            ret = gg_file_write(out_fd, GG_STR("/work/"));
            if (ret != GG_ERR_OK) {
                return ret;
            }
            ret = gg_file_write(out_fd, component_name);
            if (ret != GG_ERR_OK) {
                return ret;
            }
            return gg_file_write(out_fd, GG_STR("/"));
        }
    } else if (gg_buffer_eq(type, GG_STR("artifacts"))) {
        if (gg_buffer_eq(arg, GG_STR("path"))) {
            ret = gg_file_write(out_fd, root_path);
            if (ret != GG_ERR_OK) {
                return ret;
            }
            ret = gg_file_write(out_fd, GG_STR("/packages/"));
            if (ret != GG_ERR_OK) {
                return ret;
            }
            ret = gg_file_write(out_fd, GG_STR("artifacts/"));
            if (ret != GG_ERR_OK) {
                return ret;
            }
            ret = gg_file_write(out_fd, component_name);
            if (ret != GG_ERR_OK) {
                return ret;
            }
            ret = gg_file_write(out_fd, GG_STR("/"));
            if (ret != GG_ERR_OK) {
                return ret;
            }
            ret = gg_file_write(out_fd, component_version);
            if (ret != GG_ERR_OK) {
                return ret;
            }
            return gg_file_write(out_fd, GG_STR("/"));
        }
        if (gg_buffer_eq(arg, GG_STR("decompressedPath"))) {
            ret = gg_file_write(out_fd, root_path);
            if (ret != GG_ERR_OK) {
                return ret;
            }
            ret = gg_file_write(out_fd, GG_STR("/packages/"));
            if (ret != GG_ERR_OK) {
                return ret;
            }
            ret = gg_file_write(out_fd, GG_STR("artifacts-unarchived/"));
            if (ret != GG_ERR_OK) {
                return ret;
            }
            ret = gg_file_write(out_fd, component_name);
            if (ret != GG_ERR_OK) {
                return ret;
            }
            ret = gg_file_write(out_fd, GG_STR("/"));
            if (ret != GG_ERR_OK) {
                return ret;
            }
            ret = gg_file_write(out_fd, component_version);
            if (ret != GG_ERR_OK) {
                return ret;
            }
            return gg_file_write(out_fd, GG_STR("/"));
        }
    } else if (gg_buffer_eq(type, GG_STR("configuration"))) {
        return insert_config_value(out_fd, arg);
    }

    GG_LOGE(
        "Unhandled variable substitution: %.*s.",
        (int) escape_seq.len,
        escape_seq.data
    );
    return GG_ERR_FAILURE;
}

static GgError handle_escape(
    int out_fd,
    uint8_t **current_pointer,
    const uint8_t *end_pointer,
    GgBuffer root_path,
    GgBuffer component_name,
    GgBuffer component_version,
    GgBuffer thing_name
) {
    static uint8_t escape_contents[256];
    GgByteVec vec = GG_BYTE_VEC(escape_contents);
    (*current_pointer)++;
    while (true) {
        if (*current_pointer == end_pointer) {
            GG_LOGE("Recipe escape is not terminated.");
            return GG_ERR_INVALID;
        }
        if (**current_pointer != '}') {
            GgError ret = gg_byte_vec_push(&vec, **current_pointer);
            if (ret != GG_ERR_OK) {
                GG_LOGE("Recipe escape exceeded max length.");
                return ret;
            }
            (*current_pointer)++;
        } else {
            (*current_pointer)++;
            return substitute_escape(
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

static GgError process_set_env(
    int out_fd,
    GgMap env_values_as_map,
    GgBuffer root_path,
    GgBuffer component_name,
    GgBuffer component_version,
    GgBuffer thing_name
) {
    GG_LOGT("Lifecycle Setenv, is a map");
    GG_MAP_FOREACH (pair, env_values_as_map) {
        GgError ret = gg_file_write(out_fd, GG_STR("export "));
        if (ret != GG_ERR_OK) {
            return ret;
        }
        ret = gg_file_write(out_fd, gg_kv_key(*pair));
        if (ret != GG_ERR_OK) {
            return ret;
        }
        GG_LOGT(
            "Lifecycle Setenv, map key: %.*s",
            (int) gg_kv_key(*pair).len,
            gg_kv_key(*pair).data
        );
        ret = gg_file_write(out_fd, GG_STR("="));
        if (ret != GG_ERR_OK) {
            return ret;
        }

        if (gg_obj_type(*gg_kv_val(pair)) != GG_TYPE_BUF) {
            GG_LOGW("Invalid lifecycle Setenv, Key values must be String");
            return GG_ERR_INVALID;
        }
        GgBuffer val = gg_obj_into_buf(*gg_kv_val(pair));
        GG_LOGT("Lifecycle Setenv, map value: %.*s", (int) val.len, val.data);
        uint8_t *current_pointer = &val.data[0];
        uint8_t *end_pointer = &val.data[val.len];
        if (val.len == 0) {
            // Add in a new line if no value is provided
            ret = gg_file_write(out_fd, GG_STR("\n"));
            if (ret != GG_ERR_OK) {
                return ret;
            }
        }
        while (true) {
            if (current_pointer == end_pointer) {
                break;
            }
            if (*current_pointer != '{') {
                ret = write_escaped_char(out_fd, *current_pointer);
                if (ret != GG_ERR_OK) {
                    return ret;
                }
                current_pointer++;
            } else {
                ret = handle_escape(
                    out_fd,
                    &current_pointer,
                    end_pointer,
                    root_path,
                    component_name,
                    component_version,
                    thing_name
                );
                if (ret != GG_ERR_OK) {
                    return ret;
                }
            }
        }
        ret = gg_file_write(out_fd, GG_STR("\n"));
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }
    return GG_ERR_OK;
}

static GgError find_and_process_set_env(
    int out_fd,
    GgMap map_containing_setenv,
    GgBuffer root_path,
    GgBuffer component_name,
    GgBuffer component_version,
    GgBuffer thing_name
) {
    GgObject *env_values;
    GgError ret = GG_ERR_OK;

    if (gg_map_get(map_containing_setenv, GG_STR("Setenv"), &env_values)) {
        if (gg_obj_type(*env_values) != GG_TYPE_MAP) {
            GG_LOGE("Invalid lifecycle Setenv, Must be a map");
            return GG_ERR_INVALID;
        }

        ret = process_set_env(
            out_fd,
            gg_obj_into_map(*env_values),
            root_path,
            component_name,
            component_version,
            thing_name
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }

    } else {
        GG_LOGT("No Setenv found");
    }
    return ret;
}

static GgError process_lifecycle_phase(
    int out_fd,
    GgMap selected_lifecycle,
    GgBuffer phase,
    GgBuffer root_path,
    GgBuffer component_name,
    GgBuffer component_version,
    GgBuffer thing_name
) {
    GgBuffer selected_script_as_buf = { 0 };
    GgMap set_env_as_map = { 0 };
    bool is_root;
    GgError ret = fetch_script_section(
        selected_lifecycle,
        phase,
        &is_root,
        &selected_script_as_buf,
        &set_env_as_map,
        NULL
    );

    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (set_env_as_map.len != 0) {
        GG_LOGT(
            "Processing lifecycle phase Setenv for %.*s",
            (int) phase.len,
            phase.data
        );
        ret = process_set_env(
            out_fd,
            set_env_as_map,
            root_path,
            component_name,
            component_version,
            thing_name
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to process setenv");
            return ret;
        }
    }

    if (selected_script_as_buf.len == 0) {
        // Add in a new line if no value is provided
        ret = gg_file_write(out_fd, GG_STR("\n"));
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }
    GG_LOGT(
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
            ret = gg_file_write(out_fd, (GgBuffer) { current_pointer, 1 });
            if (ret != GG_ERR_OK) {
                return ret;
            }
            current_pointer++;
        } else {
            ret = handle_escape(
                out_fd,
                &current_pointer,
                end_pointer,
                root_path,
                component_name,
                component_version,
                thing_name
            );
            if (ret != GG_ERR_OK) {
                return ret;
            }
        }
    }
    return ret;
}

static GgError write_script_with_replacement(
    int out_fd,
    GgMap recipe_as_map,
    GgBuffer root_path,
    GgBuffer component_name,
    GgBuffer component_version,
    GgBuffer thing_name,
    GgBuffer phase
) {
    GgMap selected_lifecycle_map = { 0 };
    GgError ret
        = select_linux_lifecycle(recipe_as_map, &selected_lifecycle_map);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to find linux Lifecycle");
        return ret;
    }

    GG_LOGT("Processing Global Setenv");
    ret = find_and_process_set_env(
        out_fd,
        selected_lifecycle_map,
        root_path,
        component_name,
        component_version,
        thing_name
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to process setenv");
        return ret;
    }

    GG_LOGT(
        "Processing other Lifecycle phase: %.*s", (int) phase.len, phase.data
    );
    ret = process_lifecycle_phase(
        out_fd,
        selected_lifecycle_map,
        phase,
        root_path,
        component_name,
        component_version,
        thing_name
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to process lifecycle phase: %.*s",
            (int) phase.len,
            phase.data
        );
        return ret;
    }

    // if startup, send a ready notification before exiting
    // otherwise, simple startup scripts will fail with 'protocol' by systemd
    if (gg_buffer_eq(GG_STR("startup"), phase)) {
        ret = gg_file_write(out_fd, GG_STR("\n"));
        if (ret != GG_ERR_OK) {
            return ret;
        }
        ret = gg_file_write(out_fd, GG_STR("systemd-notify --ready\n"));
        if (ret != GG_ERR_OK) {
            return ret;
        }
        ret = gg_file_write(out_fd, GG_STR("systemd-notify --stopping\n"));
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    return GG_ERR_OK;
}

static GgError get_system_config_error_cb(
    void *ctx, GgBuffer error_code, GgBuffer message
) {
    (void) ctx;

    GG_LOGE(
        "Received PrivateGetSystemConfig error %.*s: %.*s.",
        (int) error_code.len,
        error_code.data,
        (int) message.len,
        message.data
    );

    return GG_ERR_FAILURE;
}

static GgError get_system_config_result_cb(void *ctx, GgMap result) {
    GgBuffer *resp_buf = ctx;

    GgObject *value;
    GgError ret = gg_map_validate(
        result,
        GG_MAP_SCHEMA({ GG_STR("value"), GG_REQUIRED, GG_TYPE_NULL, &value })
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed validating server response.");
        return GG_ERR_INVALID;
    }

    if (gg_obj_type(*value) != GG_TYPE_BUF) {
        GG_LOGE("Config value is not a string.");
        return GG_ERR_FAILURE;
    }

    if (resp_buf != NULL) {
        GgBuffer val_buf = gg_obj_into_buf(*value);

        GgArena alloc = gg_arena_init(*resp_buf);
        ret = gg_arena_claim_buf(&val_buf, &alloc);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Insufficent memory provided for response.");
            return ret;
        }

        *resp_buf = val_buf;
    }

    return GG_ERR_OK;
}

static GgError get_system_config(GgBuffer key, GgBuffer *value) {
    return ggipc_call(
        GG_STR("aws.greengrass.private#GetSystemConfig"),
        GG_STR("aws.greengrass.private#GetSystemConfigRequest"),
        GG_MAP(gg_kv(GG_STR("key"), gg_obj_buf(key))),
        &get_system_config_result_cb,
        &get_system_config_error_cb,
        value
    );
}

static char svcuid[GG_IPC_SVCUID_STR_LEN + 1] = { 0 };

GgError ggipc_connect_extra_header_handler(EventStreamHeaderIter headers) {
    EventStreamHeader header;
    while (eventstream_header_next(&headers, &header) == GG_ERR_OK) {
        if (gg_buffer_eq(header.name, GG_STR("svcuid"))) {
            if (header.value.type != EVENTSTREAM_STRING) {
                GG_LOGE("Response svcuid header not string.");
                return GG_ERR_INVALID;
            }

            if (header.value.string.len > GG_IPC_SVCUID_STR_LEN) {
                GG_LOGE("Response svcuid too long.");
                return GG_ERR_NOMEM;
            }

            memcpy(svcuid, header.value.string.data, header.value.string.len);
            return GG_ERR_OK;
        }
    }

    GG_LOGE("Response missing svcuid header.");
    return GG_ERR_FAILURE;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
GgError runner(const RecipeRunnerArgs *args) {
    // Get the SocketPath from Environment Variable
    char *socket_path =
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        getenv("AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT");

    if (socket_path == NULL) {
        GG_LOGE("IPC socket path env var not set.");
        return GG_ERR_FAILURE;
    }

    GgBuffer component_name = gg_buffer_from_null_term(args->component_name);

    // Fetch the SVCUID
    GgError ret = ggipc_connect_with_payload(
        gg_buffer_from_null_term(socket_path),
        gg_obj_map(
            GG_MAP(gg_kv(GG_STR("componentName"), gg_obj_buf(component_name)))
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Runner failed to authenticate with nucleus.");
        return ret;
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    int sys_ret = setenv("SVCUID", svcuid, true);
    if (sys_ret != 0) {
        GG_LOGE("setenv failed: %d.", errno);
    }
    sys_ret =
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        setenv("AWS_CONTAINER_AUTHORIZATION_TOKEN", svcuid, true);
    if (sys_ret != 0) {
        GG_LOGE("setenv failed: %d.", errno);
    }

    static uint8_t resp_mem[PATH_MAX];

    GgBuffer resp = GG_BUF(resp_mem);
    resp.len -= 1;
    ret = get_system_config(GG_STR("rootCaPath"), &resp);
    if (ret != GG_ERR_OK || resp.len == 0) {
        GG_LOGW("rootCaPath not available; GG_ROOT_CA_PATH will be empty.");
        resp.len = 0;
    }
    resp_mem[resp.len] = '\0';
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    sys_ret = setenv("GG_ROOT_CA_PATH", (char *) resp_mem, true);
    if (sys_ret != 0) {
        GG_LOGE("setenv failed: %d.", errno);
    }

    resp = GG_BUF(resp_mem);
    resp.len -= 1;
    ret = ggipc_get_config_str(
        GG_BUF_LIST(GG_STR("awsRegion")),
        &GG_STR("aws.greengrass.NucleusLite"),
        &resp
    );

    if (ret != GG_ERR_OK || resp.len == 0) {
        GG_LOGW("awsRegion not available; AWS_REGION will be empty.");
        resp.len = 0;
    }
    resp_mem[resp.len] = '\0';
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    sys_ret = setenv("AWS_REGION", (char *) resp_mem, true);
    if (sys_ret != 0) {
        GG_LOGE("setenv failed: %d.", errno);
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    sys_ret = setenv("AWS_DEFAULT_REGION", (char *) resp_mem, true);
    if (sys_ret != 0) {
        GG_LOGE("setenv failed: %d.", errno);
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    sys_ret = setenv("GGC_VERSION", GGL_VERSION, true);
    if (sys_ret != 0) {
        GG_LOGE("setenv failed: %d.", errno);
    }

    resp = GG_BUF(resp_mem);
    resp.len -= 1;
    ret = ggipc_get_config_str(
        GG_BUF_LIST(GG_STR("networkProxy"), GG_STR("proxy"), GG_STR("url")),
        &GG_STR("aws.greengrass.NucleusLite"),
        &resp
    );
    switch (ret) {
    case GG_ERR_NOMEM:
        GG_LOGE(
            "Failed to get network proxy url from config - value longer than supported."
        );
        return ret;
    case GG_ERR_NOENTRY:
        GG_LOGD("No network proxy set.");
        break;
    case GG_ERR_OK: {
        if (resp.len == 0) {
            GG_LOGD("Network proxy URL is empty.");
            break;
        }
        resp_mem[resp.len] = '\0';
        // NOLINTBEGIN(concurrency-mt-unsafe)
        setenv("all_proxy", (char *) resp_mem, true);
        setenv("ALL_PROXY", (char *) resp_mem, true);
        setenv("http_proxy", (char *) resp_mem, true);
        setenv("HTTP_PROXY", (char *) resp_mem, true);
        setenv("https_proxy", (char *) resp_mem, true);
        setenv("HTTPS_PROXY", (char *) resp_mem, true);
        // NOLINTEND(concurrency-mt-unsafe)
        break;
    }
    default:
        GG_LOGE("Failed to get proxy url from config. Error: %d.", ret);
        return ret;
    }

    resp = GG_BUF(resp_mem);
    resp.len -= 1;
    ret = ggipc_get_config_str(
        GG_BUF_LIST(GG_STR("networkProxy"), GG_STR("noProxyAddresses")),
        &GG_STR("aws.greengrass.NucleusLite"),
        &resp
    );
    switch (ret) {
    case GG_ERR_NOMEM:
        GG_LOGE(
            "Failed to get network proxy url from config - value longer than supported."
        );
        return ret;
    case GG_ERR_NOENTRY:
        GG_LOGD("No network proxy set.");
        break;
    case GG_ERR_OK: {
        if (resp.len == 0) {
            GG_LOGD("Network proxy noProxyAddresses is empty.");
            break;
        }
        resp_mem[resp.len] = '\0';
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        setenv("no_proxy", (char *) resp_mem, true);
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        setenv("NO_PROXY", (char *) resp_mem, true);
        break;
    }
    default:
        GG_LOGE("Failed to get proxy url from config. Error: %d.", ret);
        return ret;
    }

    static uint8_t thing_name_mem[MAX_THING_NAME_LEN + 1];
    GgBuffer thing_name = GG_BUF(thing_name_mem);
    thing_name.len -= 1;
    ret = get_system_config(GG_STR("thingName"), &thing_name);
    if (ret != GG_ERR_OK) {
        GG_LOGW("thingName not available; AWS_IOT_THING_NAME will be empty.");
        thing_name.len = 0;
    }
    thing_name_mem[thing_name.len] = '\0';
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    sys_ret = setenv("AWS_IOT_THING_NAME", (char *) thing_name_mem, true);
    if (sys_ret != 0) {
        GG_LOGE("setenv failed: %d.", errno);
    }

    GgBuffer root_path = GG_BUF(resp_mem);
    ret = get_system_config(GG_STR("rootPath"), &root_path);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get root path from config.");
        return ret;
    }

    int root_path_fd;
    ret = gg_dir_open(root_path, O_PATH, false, &root_path_fd);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to open rootPath.");
        return ret;
    }
    GgBuffer component_version
        = gg_buffer_from_null_term(args->component_version);

    GgBuffer phase = gg_buffer_from_null_term(args->phase);

    static uint8_t recipe_mem[GGL_COMPONENT_RECIPE_MAX_LEN];
    GgArena alloc = gg_arena_init(GG_BUF(recipe_mem));
    GgObject recipe = { 0 };
    GG_LOGT("Root Path: %.*s", (int) root_path.len, root_path.data);
    ret = ggl_recipe_get_from_file(
        root_path_fd, component_name, component_version, &alloc, &recipe
    );
    (void) gg_close(root_path_fd);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to find the recipe file");
        return ret;
    }

    // Check if TES is the dependency within the recipe
    GgObject *val;
    if (gg_map_get(
            gg_obj_into_map(recipe), GG_STR("ComponentDependencies"), &val
        )) {
        if (gg_obj_type(*val) != GG_TYPE_MAP) {
            return GG_ERR_PARSE;
        }
        GgObject *inner_val;
        GgMap inner_map = gg_obj_into_map(*val);
        if (gg_map_get(
                inner_map,
                GG_STR("aws.greengrass.TokenExchangeService"),
                &inner_val
            )) {
            static uint8_t resp_mem2[PATH_MAX];
            GgByteVec resp_vec = GG_BYTE_VEC(resp_mem2);
            ret = gg_byte_vec_append(&resp_vec, GG_STR("http://localhost:"));
            if (ret != GG_ERR_OK) {
                GG_LOGE("Failed to append http://localhost:");
                return ret;
            }
            GgBuffer rest = gg_byte_vec_remaining_capacity(resp_vec);

            ret = ggipc_get_config_str(
                GG_BUF_LIST(GG_STR("port")),
                &GG_STR("aws.greengrass.TokenExchangeService"),
                &rest
            );
            if (ret != GG_ERR_OK) {
                GG_LOGE(
                    "Failed to get port for TES server from config. Possible reason, TES server might not have started yet."
                );
                return ret;
            }
            resp_vec.buf.len += rest.len;
            ret = gg_byte_vec_append(
                &resp_vec, GG_STR("/2016-11-01/credentialprovider/\0")
            );
            if (ret != GG_ERR_OK) {
                GG_LOGE("Failed to append /2016-11-01/credentialprovider/");
                return ret;
            }

            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            sys_ret = setenv(
                "AWS_CONTAINER_CREDENTIALS_FULL_URI",
                (char *) resp_vec.buf.data,
                true
            );
            if (sys_ret != 0) {
                GG_LOGE(
                    "setenv AWS_CONTAINER_CREDENTIALS_FULL_URI failed: %d.",
                    errno
                );
            }
        }
    }
    int dir_fd;
    ret = gg_dir_open(root_path, O_PATH, false, &dir_fd);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to open %.*s.", (int) root_path.len, root_path.data);
        return ret;
    }
    int new_fd;
    ret = gg_dir_openat(dir_fd, GG_STR("work"), O_PATH, false, &new_fd);
    (void) gg_close(dir_fd);
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to open %.*s/work.", (int) root_path.len, root_path.data
        );
        return ret;
    }
    dir_fd = new_fd;
    ret = gg_dir_openat(dir_fd, component_name, O_RDONLY, false, &new_fd);
    (void) gg_close(dir_fd);
    if (ret != GG_ERR_OK) {
        GG_LOGE(
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
        GG_LOGE("Failed to change working directory: %d.", errno);
        return GG_ERR_FAILURE;
    }

    int script_fd = memfd_create("ggl_component_script", 0);
    if (script_fd < 0) {
        GG_LOGE(
            "Failed to create memfd for component phase script: %d.", errno
        );
        return GG_ERR_FAILURE;
    }

    ret = gg_file_write(script_fd, GG_STR("#!/bin/sh\n"));
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to write shebang to component phase script.");
        return ret;
    }

    ret = write_script_with_replacement(
        script_fd,
        gg_obj_into_map(recipe),
        root_path,
        component_name,
        component_version,
        thing_name,
        phase
    );

    const char *argv[] = { "/bin/sh", NULL };
    sys_ret = fexecve(script_fd, (char **) argv, environ);

    GG_LOGE("Failed to execute component phase script: %d.", errno);
    return GG_ERR_FATAL;
}
