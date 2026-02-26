// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_components.h"
#include "ipc_server.h"
#include <assert.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggipcd.h>
#include <ggl/core_bus/gg_config.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

static const GgBuffer GG_IPC_SOCKET_NAME = GG_STR("gg-ipc.socket");
uint8_t default_socket_path[PATH_MAX];

GgError run_ggipcd(GglIpcArgs *args) {
    const GgBuffer *socket_name = NULL;
    GgBuffer socket_path;

    if (args->socket_path != NULL) {
        socket_path = gg_buffer_from_null_term(args->socket_path);
    } else {
        GgArena alloc = gg_arena_init(GG_BUF(default_socket_path));
        GgBuffer path_buf;
        GgError ret = ggl_gg_config_read_str(
            GG_BUF_LIST(GG_STR("system"), GG_STR("rootPath")), &alloc, &path_buf
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to read system/rootPath from config.");
            return ret;
        }

        assert(path_buf.data == default_socket_path);
        GgByteVec path_vec
            = { .buf = path_buf, .capacity = sizeof(default_socket_path) };
        ret = gg_byte_vec_push(&path_vec, '/');
        gg_byte_vec_chain_append(&ret, &path_vec, GG_IPC_SOCKET_NAME);
        if (ret != GG_ERR_OK) {
            return ret;
        }

        socket_name = &GG_IPC_SOCKET_NAME;
        socket_path = path_vec.buf;
    }

    GgError err = ggl_ipc_start_component_server();

    if (err != GG_ERR_OK) {
        GG_LOGE("Failed to start ggl_ipc_component_server.");
        return err;
    }

    err = ggl_ipc_listen(socket_name, socket_path);

    GG_LOGE("Exiting due to error while listening (%u).", err);
    return err;
}
