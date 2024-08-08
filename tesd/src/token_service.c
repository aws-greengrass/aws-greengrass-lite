// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "token_service.h"
#include "ggl/http.h"
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/json_decode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_HTTP_RESPONSE_LENGTH 4096
#define  MAX_HTTP_RESPONSE_SUB_OBJECTS 10
static uint8_t http_response_decode_mem[MAX_HTTP_RESPONSE_SUB_OBJECTS * sizeof(GglObject)];
static GglMap server_json_creds = { 0 };

static GglError create_map_for_server(GglMap json_creds, GglMap *out_json) {
    GglObject *creds;
    bool ret = ggl_map_get(json_creds, GGL_STR("credentials"), &creds);

    if (!ret) {
        return GGL_ERR_INVALID;
    }

    if (creds->type != GGL_TYPE_MAP) {
        return GGL_ERR_INVALID;
    }

    GGL_MAP_FOREACH(pair, creds->map) {
        if (ggl_buffer_eq(pair->key, GGL_STR("accessKeyId"))) {
            pair->key = GGL_STR("AccessKeyId");
        } else if (ggl_buffer_eq(pair->key, GGL_STR("secretAccessKey"))) {
            pair->key = GGL_STR("SecretAccessKey");
        } else if (ggl_buffer_eq(pair->key, GGL_STR("sessionToken"))) {
            pair->key = GGL_STR("Token");
        } else if (ggl_buffer_eq(pair->key, GGL_STR("expiration"))) {
            pair->key = GGL_STR("Expiration");
        }
    }

    *out_json = creds->map;
    return GGL_ERR_OK;
}

static void rpc_request_creds(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;
    GGL_LOGD("request_credentials", "Handling token publish request.");

    GglObject *val;

    if (ggl_map_get(params, GGL_STR("authz_token"), &val)
        && (val->type == GGL_TYPE_BUF)) {
        GglBuffer authz_token = val->buf;
        if (authz_token.len > UINT16_MAX) {
            GGL_LOGE("request_credentials", "Publish payload too large.");
            ggl_return_err(handle, GGL_ERR_RANGE);
            return;
        }

    } else {
        GGL_LOGE("request_credentials", "Publish received invalid arguments.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }

    ggl_respond(handle, GGL_OBJ(server_json_creds));
}

static void start_tes_core_bus_server(void) {
    // Server handler
    GglRpcMethodDesc handlers[] = {
        { GGL_STR("request_credentials"), false, rpc_request_creds, NULL },
    };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GglBuffer interface = GGL_STR("/aws/ggl/tesd");

    GglError ret = ggl_listen(interface, handlers, handlers_len);

    GGL_LOGE("tesd", "Exiting with error %u.", (unsigned) ret);
}

GglError initiate_request(
    char *root_ca,
    char *cert_path,
    char *key_path,
    char *thing_name,
    char *role_alias,
    char *cert_endpoint
) {
    static char url_buf[2024] = { 0 };
    static uint8_t response_buffer[MAX_HTTP_RESPONSE_LENGTH] = { 0 };

    strncat(url_buf, "https://", strlen("https://"));
    strncat(url_buf, (char *) cert_endpoint, strlen(cert_endpoint));
    strncat(url_buf, "/role-aliases/", strlen("/role-aliases/"));
    strncat(url_buf, (char *) role_alias, strlen(role_alias));
    strncat(url_buf, "/credentials\0", strlen("/credentials\0"));

    CertificateDetails certificate = { .gghttplib_cert_path = cert_path,
                                       .gghttplib_root_ca_path = root_ca,
                                       .gghttplib_p_key_path = key_path };

    GglBuffer buffer = GGL_BUF(response_buffer);

    fetch_token(url_buf, thing_name, certificate, &buffer);
    GGL_LOGI("tesd", "The credentials received are: %s", buffer.data);

    // Create a json object from the URL response
    GglObject json_cred_obj;
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(http_response_decode_mem));
    GglError ret
        = ggl_json_decode_destructive(buffer, &balloc.alloc, &json_cred_obj);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (json_cred_obj.type != GGL_TYPE_MAP) {
        return GGL_ERR_FAILURE;
    }

    ret = create_map_for_server(json_cred_obj.map, &server_json_creds);

    start_tes_core_bus_server();

    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}
