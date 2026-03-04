// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "mqtt.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggl/core_bus/gg_config.h>
#include <iotcored.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_ENDPOINT_LEN 128
#define MAX_THINGNAME_LEN 128

static uint8_t endpoint_mem[MAX_ENDPOINT_LEN + 1] = { 0 };
static pthread_mutex_t endpoint_mtx = PTHREAD_MUTEX_INITIALIZER;

static GgError endpoint_change_callback(
    void *ctx, uint32_t handle, GgObject data
) {
    (void) ctx;
    (void) handle;
    (void) data;

    GG_MTX_SCOPE_GUARD(&endpoint_mtx);

    char old_endpoint[MAX_ENDPOINT_LEN + 1];
    memcpy(old_endpoint, endpoint_mem, sizeof(old_endpoint));

    GgArena alloc = gg_arena_init(
        gg_buffer_substr(GG_BUF(endpoint_mem), 0, sizeof(endpoint_mem) - 1)
    );
    GgBuffer new_endpoint;
    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("iotDataEndpoint")
        ),
        &alloc,
        &new_endpoint
    );
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to read updated iotDataEndpoint: %d.", ret);
        return GG_ERR_OK;
    }

    endpoint_mem[new_endpoint.len] = '\0';

    if (strncmp(old_endpoint, (char *) endpoint_mem, MAX_ENDPOINT_LEN) == 0) {
        return GG_ERR_OK;
    }

    GG_LOGI(
        "iotDataEndpoint updated from %s to %.*s",
        old_endpoint,
        (int) new_endpoint.len,
        new_endpoint.data
    );

    iotcored_mqtt_disconnect();
    return GG_ERR_OK;
}

static bool get_proxy_variable(GgBufList aliases, GgBuffer *destination) {
    for (size_t i = 0; i < aliases.len; ++i) {
        char *name = (char *) aliases.bufs[i].data;
        if (name == NULL) {
            continue;
        }
        // This is safe as long as getenv is reentrant
        // and no other threads call setenv.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        char *value = getenv(name);
        if (value == NULL) {
            continue;
        }
        GgBuffer source = gg_buffer_from_null_term(value);
        if (source.len >= destination->len) {
            GG_LOGW("%s too long.", name);
            continue;
        }
        memcpy(destination->data, source.data, source.len);
        destination->len = source.len;
        destination->data[destination->len] = '\0';
        return true;
    }
    return false;
}

static void set_proxy_args(IotcoredArgs *args) {
    static uint8_t proxy_uri_mem[PATH_MAX] = { 0 };

    if (args->proxy_uri == NULL) {
        GgBuffer proxy_uri = GG_BUF(proxy_uri_mem);
        if (get_proxy_variable(
                GG_BUF_LIST(GG_STR("https_proxy"), GG_STR("HTTPS_PROXY")),
                &proxy_uri
            )) {
            args->proxy_uri = (char *) proxy_uri_mem;
        }
    }

    if (args->proxy_uri == NULL) {
        GgArena alloc = gg_arena_init(gg_buffer_substr(
            GG_BUF(proxy_uri_mem), 0, sizeof(proxy_uri_mem) - 1
        ));
        GgBuffer proxy_uri;
        GgError ret = ggl_gg_config_read_str(
            GG_BUF_LIST(
                GG_STR("services"),
                GG_STR("aws.greengrass.NucleusLite"),
                GG_STR("configuration"),
                GG_STR("networkProxy"),
                GG_STR("proxy"),
                GG_STR("url")
            ),
            &alloc,
            &proxy_uri
        );
        if (ret == GG_ERR_OK) {
            args->proxy_uri = (char *) proxy_uri.data;
        }
    }

    static uint8_t no_proxy_mem[PATH_MAX] = { 0 };

    if (args->no_proxy == NULL) {
        GgBuffer no_proxy = GG_BUF(no_proxy_mem);
        if (get_proxy_variable(
                GG_BUF_LIST(GG_STR("no_proxy"), GG_STR("NO_PROXY")), &no_proxy
            )) {
            args->no_proxy = (char *) no_proxy_mem;
        }
    }

    if (args->no_proxy == NULL) {
        GgArena alloc = gg_arena_init(
            gg_buffer_substr(GG_BUF(no_proxy_mem), 0, sizeof(no_proxy_mem) - 1)
        );
        GgBuffer no_proxy;
        GgError ret = ggl_gg_config_read_str(
            GG_BUF_LIST(
                GG_STR("services"),
                GG_STR("aws.greengrass.NucleusLite"),
                GG_STR("configuration"),
                GG_STR("networkProxy"),
                GG_STR("noproxy"),
            ),
            &alloc,
            &no_proxy
        );
        if (ret == GG_ERR_OK) {
            args->no_proxy = (char *) no_proxy.data;
        }
    }
}

GgError run_iotcored(IotcoredArgs *args) {
    if (args->cert == NULL) {
        static uint8_t cert_mem[PATH_MAX] = { 0 };
        GgArena alloc = gg_arena_init(
            gg_buffer_substr(GG_BUF(cert_mem), 0, sizeof(cert_mem) - 1)
        );
        GgBuffer cert;

        GgError ret = ggl_gg_config_read_str(
            GG_BUF_LIST(GG_STR("system"), GG_STR("certificateFilePath")),
            &alloc,
            &cert
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
        args->cert = (char *) cert_mem;
    }

    bool endpoint_from_config = !args->endpoint;

    if (endpoint_from_config) {
        GgError ret = ggl_gg_config_subscribe(
            GG_BUF_LIST(
                GG_STR("services"),
                GG_STR("aws.greengrass.NucleusLite"),
                GG_STR("configuration"),
                GG_STR("iotDataEndpoint")
            ),
            endpoint_change_callback,
            NULL,
            NULL,
            NULL
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to subscribe to iotDataEndpoint changes: %d.", ret);
            return ret;
        }

        GG_MTX_SCOPE_GUARD(&endpoint_mtx);

        GgArena alloc = gg_arena_init(
            gg_buffer_substr(GG_BUF(endpoint_mem), 0, sizeof(endpoint_mem) - 1)
        );
        GgBuffer endpoint;

        ret = ggl_gg_config_read_str(
            GG_BUF_LIST(
                GG_STR("services"),
                GG_STR("aws.greengrass.NucleusLite"),
                GG_STR("configuration"),
                GG_STR("iotDataEndpoint")
            ),
            &alloc,
            &endpoint
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
        args->endpoint = (char *) endpoint_mem;
    }

    if (args->id == NULL) {
        static uint8_t id_mem[MAX_THINGNAME_LEN + 1] = { 0 };
        GgArena alloc = gg_arena_init(
            gg_buffer_substr(GG_BUF(id_mem), 0, sizeof(id_mem) - 1)
        );
        GgBuffer id;

        GgError ret = ggl_gg_config_read_str(
            GG_BUF_LIST(GG_STR("system"), GG_STR("thingName")), &alloc, &id
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
        args->id = (char *) id_mem;
    }

    if (args->key == NULL) {
        static uint8_t key_mem[PATH_MAX] = { 0 };
        GgArena alloc = gg_arena_init(
            gg_buffer_substr(GG_BUF(key_mem), 0, sizeof(key_mem) - 1)
        );
        GgBuffer key;

        GgError ret = ggl_gg_config_read_str(
            GG_BUF_LIST(GG_STR("system"), GG_STR("privateKeyPath")),
            &alloc,
            &key
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
        args->key = (char *) key_mem;
    }

    if (args->rootca == NULL) {
        static uint8_t rootca_mem[PATH_MAX] = { 0 };
        GgArena alloc = gg_arena_init(
            gg_buffer_substr(GG_BUF(rootca_mem), 0, sizeof(rootca_mem) - 1)
        );
        GgBuffer rootca;

        GgError ret = ggl_gg_config_read_str(
            GG_BUF_LIST(GG_STR("system"), GG_STR("rootCaPath")), &alloc, &rootca
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
        args->rootca = (char *) rootca_mem;
    }

    set_proxy_args(args);

    GgError ret = iotcored_mqtt_connect(args);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    iotcored_start_server(args);

    return GG_ERR_FAILURE;
}
