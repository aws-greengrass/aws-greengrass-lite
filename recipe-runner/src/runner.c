// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "runner.h"
#include "recipe-runner.h"
#include <sys/types.h>
#include <errno.h>
#include <ggipc/client.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <limits.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_SCRIPT_LENGTH 10000

#define MAX_THING_NAME_LEN 128

pid_t child_pid = -1; // To store child process ID

// This file is single-threaded
// NOLINTBEGIN(concurrency-mt-unsafe)

GglError runner(const RecipeRunnerArgs *args) {
    // Get the SocketPath from Environment Variable
    char *socket_path
        = getenv("AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT");

    if (socket_path == NULL) {
        GGL_LOGE("recipe-runner", "IPC socket path env var not set.");
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
        GGL_LOGE("recipe-runner", "setenv failed: %d.", errno);
    }
    sys_ret
        = setenv("AWS_CONTAINER_AUTHORIZATION_TOKEN", (char *) resp.data, true);
    if (sys_ret != 0) {
        GGL_LOGE("recipe-runner", "setenv failed: %d.", errno);
    }

    resp = GGL_BUF(resp_mem);
    resp.len -= 1;
    ret = ggipc_private_get_system_config(conn, GGL_STR("rootCaPath"), &resp);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("recipe-runner", "Failed to get root CA path from config.");
        return ret;
    }
    resp.data[resp.len] = '\0';
    sys_ret = setenv("GG_ROOT_CA_PATH", (char *) resp.data, true);
    if (sys_ret != 0) {
        GGL_LOGE("recipe-runner", "setenv failed: %d.", errno);
    }

    resp = GGL_BUF(resp_mem);
    resp.len -= 1;
    ret = ggipc_get_config_str(
        conn,
        GGL_BUF_LIST(GGL_STR("awsRegion")),
        &GGL_STR("aws.greengrass.Nucleus-Lite"),
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("recipe-runner", "Failed to get region from config.");
        return ret;
    }
    resp.data[resp.len] = '\0';
    sys_ret = setenv("AWS_REGION", (char *) resp.data, true);
    if (sys_ret != 0) {
        GGL_LOGE("recipe-runner", "setenv failed: %d.", errno);
    }
    sys_ret = setenv("AWS_DEFAULT_REGION", (char *) resp.data, true);
    if (sys_ret != 0) {
        GGL_LOGE("recipe-runner", "setenv failed: %d.", errno);
    }

    resp = GGL_BUF(resp_mem);
    resp.len -= 1;
    ret = ggipc_get_config_str(
        conn,
        GGL_BUF_LIST(GGL_STR("tesCredUrl")),
        &GGL_STR("aws.greengrass.Nucleus-Lite"),
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("recipe-runner", "Failed to get tesCredUrl from config.");
        return ret;
    }
    resp.data[resp.len] = '\0';
    sys_ret = setenv(
        "AWS_CONTAINER_CREDENTIALS_FULL_URI", (char *) resp.data, true
    );
    if (sys_ret != 0) {
        GGL_LOGE(
            "recipe-runner",
            "setenv AWS_CONTAINER_CREDENTIALS_FULL_URI failed: %d.",
            errno
        );
    }

    sys_ret = setenv("GGC_VERSION", "0.0.1", true);
    if (sys_ret != 0) {
        GGL_LOGE("recipe-runner", "setenv failed: %d.", errno);
    }

    static uint8_t thing_name_mem[MAX_THING_NAME_LEN + 1];
    GglBuffer thing_name = GGL_BUF(thing_name_mem);
    thing_name.len -= 1;
    ret = ggipc_private_get_system_config(
        conn, GGL_STR("thingName"), &thing_name
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("recipe-runner", "Failed to get thing name from config.");
        return ret;
    }
    thing_name.data[thing_name.len] = '\0';
    sys_ret = setenv("AWS_IOT_THING_NAME", (char *) thing_name.data, true);
    if (sys_ret != 0) {
        GGL_LOGE("recipe-runner", "setenv failed: %d.", errno);
    }

    GglBuffer root_path = GGL_BUF(resp_mem);
    ret = ggipc_private_get_system_config(
        conn, GGL_STR("rootPath"), &root_path
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("recipe-runner", "Failed to get root path from config.");
        return ret;
    }

    GglBuffer component_name = ggl_buffer_from_null_term(args->component_name);
    GglBuffer component_version
        = ggl_buffer_from_null_term(args->component_version);
    GglBuffer file_path = ggl_buffer_from_null_term(args->file_path);

    static uint8_t script_mem[10000];
    GglByteVec script = GGL_BYTE_VEC(script_mem);

    ret = ggl_byte_vec_append(&script, GGL_STR("sed -e 's|{artifacts:path}|"));
    ggl_byte_vec_chain_append(&ret, &script, root_path);
    ggl_byte_vec_chain_append(&ret, &script, GGL_STR("/packages/"));
    ggl_byte_vec_chain_append(&ret, &script, GGL_STR("artifacts/"));
    ggl_byte_vec_chain_append(&ret, &script, component_name);
    ggl_byte_vec_chain_push(&ret, &script, '/');
    ggl_byte_vec_chain_append(&ret, &script, component_version);
    ggl_byte_vec_chain_append(&ret, &script, GGL_STR("/|g'"));
    ggl_byte_vec_chain_append(
        &ret, &script, GGL_STR(" -e 's|{artifacts:decompressedPath}|")
    );
    ggl_byte_vec_chain_append(&ret, &script, root_path);
    ggl_byte_vec_chain_append(&ret, &script, GGL_STR("/packages/"));
    ggl_byte_vec_chain_append(&ret, &script, GGL_STR("artifacts-unarchived/"));
    ggl_byte_vec_chain_append(&ret, &script, component_name);
    ggl_byte_vec_chain_push(&ret, &script, '/');
    ggl_byte_vec_chain_append(&ret, &script, component_version);
    ggl_byte_vec_chain_append(&ret, &script, GGL_STR("/|g'"));
    ggl_byte_vec_chain_append(&ret, &script, GGL_STR(" -e 's|{work:path}|"));
    ggl_byte_vec_chain_append(&ret, &script, root_path);
    ggl_byte_vec_chain_append(&ret, &script, GGL_STR("/work/"));
    ggl_byte_vec_chain_append(&ret, &script, component_name);
    ggl_byte_vec_chain_append(&ret, &script, GGL_STR("/|g'"));
    ggl_byte_vec_chain_append(
        &ret, &script, GGL_STR(" -e 's|{kernel:rootPath}|")
    );
    ggl_byte_vec_chain_append(&ret, &script, root_path);
    ggl_byte_vec_chain_append(&ret, &script, GGL_STR("/|g'"));
    ggl_byte_vec_chain_append(
        &ret, &script, GGL_STR(" -e 's|{iot:thingName}|")
    );
    ggl_byte_vec_chain_append(&ret, &script, thing_name);
    ggl_byte_vec_chain_append(&ret, &script, GGL_STR("/|g' '"));
    ggl_byte_vec_chain_append(&ret, &script, file_path);
    ggl_byte_vec_chain_append(&ret, &script, GGL_STR("' | bash\n\0"));

    char *exec_args[] = { "bash", "-c", (char *) script.buf.data, NULL };
    execvp("bash", exec_args);
    return GGL_ERR_FAILURE;
}

// NOLINTEND(concurrency-mt-unsafe)
