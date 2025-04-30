// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggipcd.h"
#include "ipc_components.h"
#include "ipc_server.h"
#include <assert.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/vector.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

static const GglBuffer GG_IPC_SOCKET_NAME = GGL_STR("gg-ipc.socket");
uint8_t default_socket_path[PATH_MAX];

GglError run_ggipcd(GglIpcArgs *args) {
    const GglBuffer *socket_name = NULL;
    GglBuffer socket_path;

    if (args->socket_path != NULL) {
        socket_path = ggl_buffer_from_null_term(args->socket_path);
    } else {
        GglArena alloc = ggl_arena_init(GGL_BUF(default_socket_path));
        GglBuffer path_buf;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootPath")),
            &alloc,
            &path_buf
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to read system/rootPath from config.");
            return ret;
        }

        assert(path_buf.data == default_socket_path);
        GglByteVec path_vec
            = { .buf = path_buf, .capacity = sizeof(default_socket_path) };
        ret = ggl_byte_vec_push(&path_vec, '/');
        ggl_byte_vec_chain_append(&ret, &path_vec, GG_IPC_SOCKET_NAME);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        socket_name = &GG_IPC_SOCKET_NAME;
        socket_path = path_vec.buf;
    }

    GglError err = ggl_ipc_start_component_server();

    if (err != GGL_ERR_OK) {
        GGL_LOGE("Failed to start ggl_ipc_component_server.");
        return err;
    }

    err = ggl_ipc_listen(socket_name, socket_path);

    GGL_LOGE("Exiting due to error while listening (%u).", err);
    return err;
}
