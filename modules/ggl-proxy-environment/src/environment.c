// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/proxy/environment.h"
#include <errno.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static GglError setenv_wrapper(GglBufList aliases, GglBuffer value) {
    for (size_t i = 0; i < aliases.len; ++i) {
        GglBuffer name = aliases.bufs[i];
        int ret
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            = setenv((const char *) name.data, (const char *) value.data, true);
        if (ret != 0) {
            GGL_LOGE("setenv() failed with errno=%d.", errno);
            return GGL_ERR_FATAL;
        }
    }
    return GGL_ERR_OK;
}

GglError ggl_proxy_set_environment(void) {
    static uint8_t proxy_url_mem[4096] = { 0 };
    static uint8_t no_proxy_mem[4096] = { 0 };

    GglBuffer proxy_url = GGL_BUF(proxy_url_mem);
    proxy_url.len -= 1;

    GglBuffer no_proxy = GGL_BUF(no_proxy_mem);
    no_proxy.len -= 1;

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("configuration"),
            GGL_STR("networkProxy"),
            GGL_STR("proxy"),
            GGL_STR("url")
        ),
        &proxy_url
    );
    if (ret == GGL_ERR_OK) {
        GGL_LOGD("Setting proxy environment variables from config.");
        GglBufList proxy_aliases = GGL_BUF_LIST(
            GGL_STR("all_proxy"),
            GGL_STR("http_proxy"),
            GGL_STR("https_proxy"),
            GGL_STR("ALL_PROXY"),
            GGL_STR("HTTP_PROXY"),
            GGL_STR("HTTPS_PROXY")
        );
        ret = setenv_wrapper(proxy_aliases, proxy_url);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    } else if (ret != GGL_ERR_NOENTRY) {
        return GGL_ERR_FAILURE;
    }

    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("configuration"),
            GGL_STR("networkProxy"),
            GGL_STR("noProxyAddresses")
        ),
        &no_proxy
    );
    if (ret == GGL_ERR_OK) {
        GGL_LOGD("Setting noproxy list from config.");

        GglBufList no_proxy_aliases
            = GGL_BUF_LIST(GGL_STR("no_proxy"), GGL_STR("NO_PROXY"));
        ret = setenv_wrapper(no_proxy_aliases, no_proxy);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    } else if (ret != GGL_ERR_NOENTRY) {
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}
