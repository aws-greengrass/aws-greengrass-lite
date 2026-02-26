// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <gg/arena.h>
#include <gg/attr.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/proxy/environment.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

NULL_TERMINATED_STRING_ARG(2)
static GgError setenv_wrapper(GgBufList aliases, const char value[static 1]) {
    for (size_t i = 0; i < aliases.len; ++i) {
        GgBuffer name = aliases.bufs[i];
        assert(name.len > 0);
        assert(name.data != NULL);
        assert(name.data[name.len] == '\0');
        int ret
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            = setenv((const char *) name.data, value, true);
        if (ret != 0) {
            GG_LOGE("setenv() failed with errno=%d.", errno);
            return GG_ERR_FATAL;
        }
    }
    return GG_ERR_OK;
}

GgError ggl_proxy_set_environment(void) {
    uint8_t alloc_mem[4096] = { 0 };
    GgArena alloc = gg_arena_init(
        gg_buffer_substr(GG_BUF(alloc_mem), 0, sizeof(alloc_mem) - 1)
    );
    GgBuffer proxy_url;

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
        &proxy_url
    );
    if (ret == GG_ERR_OK) {
        if (proxy_url.len == 0) {
            proxy_url = GG_STR("");
        } else {
            assert(proxy_url.data != NULL);
            proxy_url.data[proxy_url.len] = '\0';
        }
        GG_LOGD("Setting proxy environment variables from config.");
        GgBufList proxy_aliases = GG_BUF_LIST(
            GG_STR("all_proxy"),
            GG_STR("http_proxy"),
            GG_STR("https_proxy"),
            GG_STR("ALL_PROXY"),
            GG_STR("HTTP_PROXY"),
            GG_STR("HTTPS_PROXY")
        );
        ret = setenv_wrapper(proxy_aliases, (char *) proxy_url.data);
        if (ret != GG_ERR_OK) {
            return ret;
        }
    } else if (ret != GG_ERR_NOENTRY) {
        return GG_ERR_FAILURE;
    }

    alloc = gg_arena_init(
        gg_buffer_substr(GG_BUF(alloc_mem), 0, sizeof(alloc_mem) - 1)
    );
    GgBuffer no_proxy;

    ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("networkProxy"),
            GG_STR("noProxyAddresses")
        ),
        &alloc,
        &no_proxy
    );
    if (ret == GG_ERR_OK) {
        if (no_proxy.len == 0) {
            no_proxy = GG_STR("");
        } else {
            assert(no_proxy.data != NULL);
            no_proxy.data[no_proxy.len] = '\0';
        }
        GG_LOGD("Setting noproxy list from config.");

        GgBufList no_proxy_aliases
            = GG_BUF_LIST(GG_STR("no_proxy"), GG_STR("NO_PROXY"));
        ret = setenv_wrapper(no_proxy_aliases, (char *) no_proxy.data);
        if (ret != GG_ERR_OK) {
            return ret;
        }
    } else if (ret != GG_ERR_NOENTRY) {
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}
