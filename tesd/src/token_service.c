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
#include <ggl/vector.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_HTTP_RESPONSE_LENGTH 4096
#define MAX_HTTP_RESPONSE_SUB_OBJECTS 10
#define MAX_PATH_LENGTH 512

typedef struct {
    char root_ca_path[MAX_PATH_LENGTH];
    char cert_path[MAX_PATH_LENGTH];
    char key_path[MAX_PATH_LENGTH];
    char thing_name[128];
    char role_alias[128];
    char url[2048];
} CredRequestT;

static uint8_t
    http_response_decode_mem[MAX_HTTP_RESPONSE_SUB_OBJECTS * sizeof(GglObject)];

static CredRequestT global_cred_details = { 0 };
static uint8_t global_response_buffer[MAX_HTTP_RESPONSE_LENGTH] = { 0 };

static GglBuffer request_token_from_aws(void) {
    memset(global_response_buffer, '\0', MAX_HTTP_RESPONSE_LENGTH);

    CertificateDetails certificate
        = { .gghttplib_cert_path = global_cred_details.cert_path,
            .gghttplib_root_ca_path = global_cred_details.root_ca_path,
            .gghttplib_p_key_path = global_cred_details.key_path };

    GglBuffer buffer = GGL_BUF(global_response_buffer);

    fetch_token(
        global_cred_details.url,
        ggl_buffer_from_null_term(global_cred_details.thing_name),
        certificate,
        &buffer
    );
    GGL_LOGI("tesd", "The credentials received are: %s", buffer.data);
    return buffer;
}

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

    (void) params;
    GglBuffer response = request_token_from_aws();

    // Create a json object from the URL response
    GglObject json_cred_obj;
    GglBumpAlloc balloc
        = ggl_bump_alloc_init(GGL_BUF(http_response_decode_mem));
    GglError ret
        = ggl_json_decode_destructive(response, &balloc.alloc, &json_cred_obj);
    if (ret != GGL_ERR_OK) {
        ggl_return_err(handle, ret);
        return;
    }

    GglObject *creds;
    bool ret_contains
        = ggl_map_get(json_cred_obj.map, GGL_STR("credentials"), &creds);

    if (!ret_contains) {
        GGL_LOGD("request_credentials", "Request failed, Invalid credentials");
        ggl_return_err(handle, ret);
        return;
    }

    GGL_LOGE(
        "request_credentials",
        "Received token, sessionToken: %.*s",
        (int) creds->map.pairs[2].val.buf.len,
        creds->map.pairs[2].val.buf.data
    );
    ggl_respond(handle, *creds);
}

static void rpc_request_formatted_creds(
    void *ctx, GglMap params, uint32_t handle
) {
    (void) ctx;
    (void) params;
    GGL_LOGD(
        "request_credentials_formatted",
        "Handling token publish request for TES server."
    );

    GglBuffer buffer = request_token_from_aws();

    // Create a json object from the URL response
    GglObject json_cred_obj;
    GglBumpAlloc balloc
        = ggl_bump_alloc_init(GGL_BUF(http_response_decode_mem));
    GglError ret
        = ggl_json_decode_destructive(buffer, &balloc.alloc, &json_cred_obj);
    if (ret != GGL_ERR_OK) {
        ggl_return_err(handle, ret);
        return;
    }

    if (json_cred_obj.type != GGL_TYPE_MAP) {
        ggl_return_err(handle, GGL_ERR_FAILURE);
        return;
    }

    static GglMap server_json_creds = { 0 };
    ret = create_map_for_server(json_cred_obj.map, &server_json_creds);

    if (ret != GGL_ERR_OK) {
        ggl_return_err(handle, ret);
        return;
    }

    ggl_respond(handle, GGL_OBJ(server_json_creds));
}

static void start_tes_core_bus_server(void) {
    // Server handler
    GglRpcMethodDesc handlers[] = {
        { GGL_STR("request_credentials"), false, rpc_request_creds, NULL },
        { GGL_STR("request_credentials_formatted"),
          false,
          rpc_request_formatted_creds,
          NULL },
    };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GglBuffer interface = GGL_STR("/aws/ggl/tesd");

    GglError ret = ggl_listen(interface, handlers, handlers_len);

    GGL_LOGE("tesd", "Exiting with error %u.", (unsigned) ret);
}

GglError initiate_request(
    const char *root_ca,
    const char *cert_path,
    const char *key_path,
    char *thing_name,
    char *role_alias,
    char *cert_endpoint
) {
    GglByteVec url_vec = GGL_BYTE_VEC(global_cred_details.url);

    GglError ret = ggl_byte_vec_append(&url_vec, GGL_STR("https://"));
    ggl_byte_vec_chain_append(
        &ret, &url_vec, ggl_buffer_from_null_term(cert_endpoint)
    );
    ggl_byte_vec_chain_append(&ret, &url_vec, GGL_STR("/role-aliases/"));
    ggl_byte_vec_chain_append(
        &ret, &url_vec, ggl_buffer_from_null_term(role_alias)
    );
    ggl_byte_vec_chain_append(&ret, &url_vec, GGL_STR("/credentials\0"));
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("tesd", "Failed to construct request URL.");
        return ret;
    }

    memcpy(global_cred_details.root_ca_path, root_ca, strlen(root_ca));
    memcpy(global_cred_details.key_path, key_path, strlen(key_path));
    memcpy(global_cred_details.thing_name, thing_name, strlen(thing_name));
    memcpy(global_cred_details.role_alias, role_alias, strlen(role_alias));
    memcpy(global_cred_details.cert_path, cert_path, strlen(cert_path));

    start_tes_core_bus_server();

    return GGL_ERR_OK;
}
