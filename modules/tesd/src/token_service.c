// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "token_service.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/json_decode.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/vector.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/core_bus/server.h>
#include <ggl/http.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_HTTP_RESPONSE_LENGTH 8192
// Number of KVs received from cloud +1 extra just in case
#define MAX_HTTP_RESPONSE_KVS 7
#define MAX_CRED_ENDPOINT_LEN 128
#define MAX_ROLE_ALIAS_LEN 128

typedef struct {
    char root_ca_path[PATH_MAX];
    char cert_path[PATH_MAX];
    char key_path[PATH_MAX];
    char thing_name[128 + 1];
    char role_alias[MAX_ROLE_ALIAS_LEN + 1];
    char cred_endpoint[MAX_CRED_ENDPOINT_LEN + 1];
    char url[2048];
} CredRequestT;

static uint8_t http_response_decode_mem[MAX_HTTP_RESPONSE_KVS * sizeof(GgKV)];

static CredRequestT global_cred_details = { 0 };
static uint8_t global_response_buffer[MAX_HTTP_RESPONSE_LENGTH] = { 0 };
static pthread_mutex_t cred_details_mtx = PTHREAD_MUTEX_INITIALIZER;

static void rebuild_url(void) {
    memset(global_cred_details.url, 0, sizeof(global_cred_details.url));
    GgByteVec url_vec = GG_BYTE_VEC(global_cred_details.url);
    GgError ret = gg_byte_vec_append(&url_vec, GG_STR("https://"));
    gg_byte_vec_chain_append(
        &ret,
        &url_vec,
        gg_buffer_from_null_term(global_cred_details.cred_endpoint)
    );
    gg_byte_vec_chain_append(&ret, &url_vec, GG_STR("/role-aliases/"));
    gg_byte_vec_chain_append(
        &ret, &url_vec, gg_buffer_from_null_term(global_cred_details.role_alias)
    );
    gg_byte_vec_chain_append(&ret, &url_vec, GG_STR("/credentials\0"));
    if (ret != GG_ERR_OK) {
        // URL construction should not fail with valid iotCredEndpoint
        // and iotRoleAlias lengths.
        GG_LOGE("Failed to construct credential URL.");
        return;
    }
    GG_LOGD("Credential URL: %s", global_cred_details.url);
}

/// Compare old and new credential config values, update changed fields,
/// and rebuild the URL. Caller must hold cred_details_mtx.
static void apply_cred_config_changes(
    const char *old_ep,
    const char *new_ep,
    size_t new_ep_len,
    const char *old_alias,
    const char *new_alias,
    size_t new_alias_len
) {
    if ((strncmp(old_ep, new_ep, MAX_CRED_ENDPOINT_LEN) == 0)
        && (strncmp(old_alias, new_alias, MAX_ROLE_ALIAS_LEN) == 0)) {
        return;
    }

    if (strncmp(old_ep, new_ep, MAX_CRED_ENDPOINT_LEN) != 0) {
        GG_LOGI(
            "iotCredEndpoint updated from %s to %.*s",
            old_ep,
            (int) new_ep_len,
            new_ep
        );
        memcpy(global_cred_details.cred_endpoint, new_ep, new_ep_len + 1);
    }

    if (strncmp(old_alias, new_alias, MAX_ROLE_ALIAS_LEN) != 0) {
        GG_LOGI(
            "iotRoleAlias updated from %s to %.*s",
            old_alias,
            (int) new_alias_len,
            new_alias
        );
        memcpy(global_cred_details.role_alias, new_alias, new_alias_len + 1);
    }

    rebuild_url();
}

void tes_update_cred_url(void) {
    GG_MTX_SCOPE_GUARD(&cred_details_mtx);

    char old_endpoint[MAX_CRED_ENDPOINT_LEN + 1];
    char old_alias[MAX_ROLE_ALIAS_LEN + 1];
    memcpy(
        old_endpoint, global_cred_details.cred_endpoint, sizeof(old_endpoint)
    );
    memcpy(old_alias, global_cred_details.role_alias, sizeof(old_alias));

    uint8_t ep_mem[MAX_CRED_ENDPOINT_LEN + 1] = { 0 };
    GgArena ep_alloc = gg_arena_init(
        gg_buffer_substr(GG_BUF(ep_mem), 0, MAX_CRED_ENDPOINT_LEN)
    );
    GgBuffer new_ep;
    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("iotCredEndpoint")
        ),
        &ep_alloc,
        &new_ep
    );
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to read updated iotCredEndpoint: %d.", ret);
        return;
    }

    uint8_t alias_mem[MAX_ROLE_ALIAS_LEN + 1] = { 0 };
    GgArena alias_alloc = gg_arena_init(
        gg_buffer_substr(GG_BUF(alias_mem), 0, MAX_ROLE_ALIAS_LEN)
    );
    GgBuffer new_alias;
    ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("iotRoleAlias")
        ),
        &alias_alloc,
        &new_alias
    );
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to read updated iotRoleAlias: %d.", ret);
        return;
    }

    ep_mem[new_ep.len] = '\0';
    alias_mem[new_alias.len] = '\0';

    apply_cred_config_changes(
        old_endpoint,
        (char *) ep_mem,
        new_ep.len,
        old_alias,
        (char *) alias_mem,
        new_alias.len
    );
}

static GgError request_token_from_aws(GgBuffer *response) {
    GG_MTX_SCOPE_GUARD(&cred_details_mtx);

    memset(global_response_buffer, '\0', MAX_HTTP_RESPONSE_LENGTH);

    CertificateDetails certificate
        = { .gghttplib_cert_path = global_cred_details.cert_path,
            .gghttplib_root_ca_path = global_cred_details.root_ca_path,
            .gghttplib_p_key_path = global_cred_details.key_path };

    GgBuffer buffer = GG_BUF(global_response_buffer);

    GG_LOGD(
        "Fetching token from credentials endpoint=%s", global_cred_details.url
    );

    GgError ret = fetch_token(
        global_cred_details.url,
        gg_buffer_from_null_term(global_cred_details.thing_name),
        certificate,
        &buffer
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get TES credentials.");
        return ret;
    }

    GG_LOGI("The TES credentials have been received");
    *response = buffer;
    return GG_ERR_OK;
}

static GgError create_map_for_server(GgMap json_creds, GgMap *out_json) {
    GgObject *creds_obj;
    bool ret = gg_map_get(json_creds, GG_STR("credentials"), &creds_obj);
    if (!ret) {
        GG_LOGE("TES response missing credentials.");
        return GG_ERR_INVALID;
    }

    if (gg_obj_type(*creds_obj) != GG_TYPE_MAP) {
        GG_LOGE("TES response credentials not a JSON object.");
        return GG_ERR_INVALID;
    }
    GgMap creds = gg_obj_into_map(*creds_obj);

    GG_MAP_FOREACH (pair, creds) {
        if (gg_buffer_eq(gg_kv_key(*pair), GG_STR("accessKeyId"))) {
            gg_kv_set_key(pair, GG_STR("AccessKeyId"));
        } else if (gg_buffer_eq(gg_kv_key(*pair), GG_STR("secretAccessKey"))) {
            gg_kv_set_key(pair, GG_STR("SecretAccessKey"));
        } else if (gg_buffer_eq(gg_kv_key(*pair), GG_STR("sessionToken"))) {
            gg_kv_set_key(pair, GG_STR("Token"));
        } else if (gg_buffer_eq(gg_kv_key(*pair), GG_STR("expiration"))) {
            gg_kv_set_key(pair, GG_STR("Expiration"));
        }
    }

    *out_json = creds;
    return GG_ERR_OK;
}

static GgError rpc_request_creds(void *ctx, GgMap params, uint32_t handle) {
    (void) ctx;
    GG_LOGD("Handling token publish request.");

    (void) params;
    GgBuffer response = { 0 };
    GgError ret = request_token_from_aws(&response);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // Create a json object from the URL response
    GgObject json_cred_obj;
    GgArena alloc = gg_arena_init(GG_BUF(http_response_decode_mem));
    ret = gg_json_decode_destructive(response, &alloc, &json_cred_obj);
    if (ret != GG_ERR_OK) {
        GG_LOGE("TES response not valid JSON.");
        return ret;
    }

    GG_LOGT("Received TES response: %.*s", (int) response.len, response.data);

    if (gg_obj_type(json_cred_obj) != GG_TYPE_MAP) {
        GG_LOGE("JSON response is not an object.");
        return GG_ERR_FAILURE;
    }

    GgObject *creds;
    bool ret_contains = gg_map_get(
        gg_obj_into_map(json_cred_obj), GG_STR("credentials"), &creds
    );

    if (!ret_contains) {
        GG_LOGE("Request failed, Invalid credentials");
        return GG_ERR_FAILURE;
    }

    ggl_respond(handle, *creds);
    return GG_ERR_OK;
}

static GgError rpc_request_formatted_creds(
    void *ctx, GgMap params, uint32_t handle
) {
    (void) ctx;
    (void) params;
    GG_LOGD("Handling token publish request for TES server.");

    GgBuffer response = { 0 };
    GgError ret = request_token_from_aws(&response);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // Create a json object from the URL response
    GgObject json_cred_obj;
    GgArena alloc = gg_arena_init(GG_BUF(http_response_decode_mem));
    ret = gg_json_decode_destructive(response, &alloc, &json_cred_obj);
    if (ret != GG_ERR_OK) {
        GG_LOGE("TES response not valid JSON.");
        return ret;
    }

    if (gg_obj_type(json_cred_obj) != GG_TYPE_MAP) {
        GG_LOGE("TES response not a JSON object.");
        return GG_ERR_FAILURE;
    }

    static GgMap server_json_creds = { 0 };
    ret = create_map_for_server(
        gg_obj_into_map(json_cred_obj), &server_json_creds
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ggl_respond(handle, gg_obj_map(server_json_creds));
    return GG_ERR_OK;
}

// Server handler
static void start_tes_core_bus_server(GgBuffer interface_name) {
    GglRpcMethodDesc handlers[] = {
        { GG_STR("request_credentials"), false, rpc_request_creds, NULL },
        { GG_STR("request_credentials_formatted"),
          false,
          rpc_request_formatted_creds,
          NULL },
    };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GgBuffer interface = GG_STR("aws_iot_tes");
    if (interface_name.len > 0) {
        interface = interface_name;
    }

    GgError ret = ggl_listen(interface, handlers, handlers_len);

    GG_LOGE("Exiting with error %u.", (unsigned) ret);
}

GgError initiate_request(
    GgBuffer root_ca,
    GgBuffer cert_path,
    GgBuffer key_path,
    GgBuffer thing_name,
    GgBuffer role_alias,
    GgBuffer cred_endpoint,
    GgBuffer interface_name
) {
    {
        GG_MTX_SCOPE_GUARD(&cred_details_mtx);

        if (cred_endpoint.len > MAX_CRED_ENDPOINT_LEN) {
            GG_LOGE(
                "iotCredEndpoint exceeds max length (%d): %.*s",
                MAX_CRED_ENDPOINT_LEN,
                (int) cred_endpoint.len,
                cred_endpoint.data
            );
            return GG_ERR_RANGE;
        }
        if (role_alias.len > MAX_ROLE_ALIAS_LEN) {
            GG_LOGE(
                "iotRoleAlias exceeds max length (%d): %.*s",
                MAX_ROLE_ALIAS_LEN,
                (int) role_alias.len,
                role_alias.data
            );
            return GG_ERR_RANGE;
        }

        memcpy(global_cred_details.root_ca_path, root_ca.data, root_ca.len);
        memcpy(global_cred_details.key_path, key_path.data, key_path.len);
        memcpy(global_cred_details.cert_path, cert_path.data, cert_path.len);
        memcpy(global_cred_details.thing_name, thing_name.data, thing_name.len);
        memcpy(global_cred_details.role_alias, role_alias.data, role_alias.len);
        global_cred_details.role_alias[role_alias.len] = '\0';
        memcpy(
            global_cred_details.cred_endpoint,
            cred_endpoint.data,
            cred_endpoint.len
        );
        global_cred_details.cred_endpoint[cred_endpoint.len] = '\0';

        rebuild_url();
    }

    start_tes_core_bus_server(interface_name);

    return GG_ERR_OK;
}
