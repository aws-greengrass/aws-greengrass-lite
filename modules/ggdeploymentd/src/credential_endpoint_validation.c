// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "credential_endpoint_validation.h"
#include <gg/arena.h>
#include <gg/backoff.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/vector.h>
#include <ggl/core_bus/client.h>
#include <ggl/process.h>
#include <limits.h>
#include <string.h>
#include <stdint.h>

#define TESD_INSTANCE_NAME "tesddeploy"
#define MAX_CRED_ENDPOINT_LEN 128
#define MAX_ROLE_ALIAS_LEN 128

typedef struct {
    GglProcessHandle handle;
} TesdInstance;

static GgError tesd_instance_start(
    TesdInstance *ctx,
    const char *tesd_path,
    const char *cred_endpoint,
    const char *role_alias
) {
    ctx->handle = (GglProcessHandle) { -1 };

    const char *args[] = {
        tesd_path,     "-n", TESD_INSTANCE_NAME, "-e",
        cred_endpoint, "-a", role_alias,         NULL,
    };

    GgError ret = ggl_process_spawn(args, NULL, &ctx->handle);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to spawn tesd instance.");
        ctx->handle = (GglProcessHandle) { -1 };
        return ret;
    }

    GG_LOGD("Spawned tesd instance (pid=%d).", ctx->handle.val);
    return GG_ERR_OK;
}

static GgError try_fetch_credentials(void *ctx) {
    (void) ctx;
    static uint8_t cred_mem[1500];
    GgArena alloc = gg_arena_init(GG_BUF(cred_mem));
    GgObject result;
    GgMap params = { 0 };

    return ggl_call(
        GG_STR(TESD_INSTANCE_NAME),
        GG_STR("request_credentials"),
        params,
        NULL,
        &alloc,
        &result
    );
}

static void tesd_instance_stop(TesdInstance *ctx) {
    if (ctx->handle.val > 0) {
        GG_LOGD("Stopping tesd instance (pid=%d).", ctx->handle.val);
        (void) ggl_process_kill(ctx->handle, 5);
        ctx->handle = (GglProcessHandle) { -1 };
    }
}

GgError check_credential_endpoint(
    GgBuffer cred_endpoint, GgBuffer role_alias, const char *bin_path
) {
    static char tesd_path_buf[PATH_MAX];
    GgByteVec tesd_path = GG_BYTE_VEC(tesd_path_buf);
    GgError ret = gg_byte_vec_append(
        &tesd_path, gg_buffer_from_null_term((char *) bin_path)
    );
    gg_byte_vec_chain_append(&ret, &tesd_path, GG_STR("tesd"));
    gg_byte_vec_chain_push(&ret, &tesd_path, '\0');
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to resolve tesd path.");
        return ret;
    }

    // Null-terminate GgBuffers for argv
    char ep_buf[MAX_CRED_ENDPOINT_LEN + 1];
    if (cred_endpoint.len >= sizeof(ep_buf)) {
        GG_LOGE("Credential endpoint too long.");
        return GG_ERR_RANGE;
    }
    memcpy(ep_buf, cred_endpoint.data, cred_endpoint.len);
    ep_buf[cred_endpoint.len] = '\0';
    char alias_buf[MAX_ROLE_ALIAS_LEN + 1];
    if (role_alias.len >= sizeof(alias_buf)) {
        GG_LOGE("Role alias too long.");
        return GG_ERR_RANGE;
    }
    memcpy(alias_buf, role_alias.data, role_alias.len);
    alias_buf[role_alias.len] = '\0';

    GG_LOGI("Checking credential endpoint %s.", ep_buf);

    TesdInstance instance = { .handle = { -1 } };
    ret = tesd_instance_start(&instance, tesd_path_buf, ep_buf, alias_buf);
    GG_CLEANUP(tesd_instance_stop, instance);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // 500ms base with exponential backoff, 60s max interval, 8 attempts.
    ret = gg_backoff(500, 60000, 8, try_fetch_credentials, NULL);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Credential endpoint validation failed for %s.", ep_buf);
    } else {
        GG_LOGI("Credential endpoint validation passed for %s.", ep_buf);
    }
    return ret;
}
