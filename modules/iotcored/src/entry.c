// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "iotcored.h"
#include "mqtt.h"
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_ENDPOINT_LEN 128
#define MAX_THINGNAME_LEN 128

static bool get_proxy_variable(GglBufList aliases, GglBuffer *destination) {
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
        GglBuffer source = ggl_buffer_from_null_term(value);
        if (source.len >= destination->len) {
            GGL_LOGW("%s too long.", name);
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
        GglBuffer proxy_uri = GGL_BUF(proxy_uri_mem);
        if (get_proxy_variable(
                GGL_BUF_LIST(GGL_STR("https_proxy"), GGL_STR("HTTPS_PROXY")),
                &proxy_uri
            )) {
            args->proxy_uri = (char *) proxy_uri_mem;
        }
    }

    if (args->proxy_uri == NULL) {
        GglArena alloc = ggl_arena_init(ggl_buffer_substr(
            GGL_BUF(proxy_uri_mem), 0, sizeof(proxy_uri_mem) - 1
        ));
        GglBuffer proxy_uri;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.NucleusLite"),
                GGL_STR("configuration"),
                GGL_STR("networkProxy"),
                GGL_STR("proxy"),
                GGL_STR("url")
            ),
            &alloc,
            &proxy_uri
        );
        if (ret == GGL_ERR_OK) {
            args->proxy_uri = (char *) proxy_uri.data;
        }
    }

    static uint8_t no_proxy_mem[PATH_MAX] = { 0 };

    if (args->no_proxy == NULL) {
        GglBuffer no_proxy = GGL_BUF(no_proxy_mem);
        if (get_proxy_variable(
                GGL_BUF_LIST(GGL_STR("no_proxy"), GGL_STR("NO_PROXY")),
                &no_proxy
            )) {
            args->no_proxy = (char *) no_proxy_mem;
        }
    }

    if (args->no_proxy == NULL) {
        GglArena alloc = ggl_arena_init(ggl_buffer_substr(
            GGL_BUF(no_proxy_mem), 0, sizeof(no_proxy_mem) - 1
        ));
        GglBuffer no_proxy;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.NucleusLite"),
                GGL_STR("configuration"),
                GGL_STR("networkProxy"),
                GGL_STR("noproxy"),
            ),
            &alloc,
            &no_proxy
        );
        if (ret == GGL_ERR_OK) {
            args->no_proxy = (char *) no_proxy.data;
        }
    }
}

GglError run_iotcored(IotcoredArgs *args) {
    if (args->cert == NULL) {
        static uint8_t cert_mem[PATH_MAX] = { 0 };
        GglArena alloc = ggl_arena_init(
            ggl_buffer_substr(GGL_BUF(cert_mem), 0, sizeof(cert_mem) - 1)
        );
        GglBuffer cert;

        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(GGL_STR("system"), GGL_STR("certificateFilePath")),
            &alloc,
            &cert
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        args->cert = (char *) cert_mem;
    }

    if (args->endpoint == NULL) {
        static uint8_t endpoint_mem[MAX_ENDPOINT_LEN + 1] = { 0 };
        GglArena alloc = ggl_arena_init(ggl_buffer_substr(
            GGL_BUF(endpoint_mem), 0, sizeof(endpoint_mem) - 1
        ));
        GglBuffer endpoint;

        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.NucleusLite"),
                GGL_STR("configuration"),
                GGL_STR("iotDataEndpoint")
            ),
            &alloc,
            &endpoint
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        args->endpoint = (char *) endpoint_mem;
    }

    if (args->id == NULL) {
        static uint8_t id_mem[MAX_THINGNAME_LEN + 1] = { 0 };
        GglArena alloc = ggl_arena_init(
            ggl_buffer_substr(GGL_BUF(id_mem), 0, sizeof(id_mem) - 1)
        );
        GglBuffer id;

        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(GGL_STR("system"), GGL_STR("thingName")), &alloc, &id
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        args->id = (char *) id_mem;
    }

    if (args->key == NULL) {
        static uint8_t key_mem[PATH_MAX] = { 0 };
        GglArena alloc = ggl_arena_init(
            ggl_buffer_substr(GGL_BUF(key_mem), 0, sizeof(key_mem) - 1)
        );
        GglBuffer key;

        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(GGL_STR("system"), GGL_STR("privateKeyPath")),
            &alloc,
            &key
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        args->key = (char *) key_mem;
    }

    if (args->rootca == NULL) {
        static uint8_t rootca_mem[PATH_MAX] = { 0 };
        GglArena alloc = ggl_arena_init(
            ggl_buffer_substr(GGL_BUF(rootca_mem), 0, sizeof(rootca_mem) - 1)
        );
        GglBuffer rootca;

        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootCaPath")),
            &alloc,
            &rootca
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        args->rootca = (char *) rootca_mem;
    }

    set_proxy_args(args);

    GglError ret = iotcored_mqtt_connect(args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    iotcored_start_server(args);

    return GGL_ERR_FAILURE;
}
