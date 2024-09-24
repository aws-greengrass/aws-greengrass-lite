// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "mqttproxy.h"
#include "../../ipc_service.h"
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <regex.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static GglIpcOperation operations[] = {
    {
        GGL_STR("aws.greengrass#PublishToIoTCore"),
        ggl_handle_publish_to_iot_core,
    },
    {
        GGL_STR("aws.greengrass#SubscribeToIoTCore"),
        ggl_handle_subscribe_to_iot_core,
    },
};

GglIpcService ggl_ipc_service_mqttproxy = {
    .name = GGL_STR("aws.greengrass.ipc.mqttproxy"),
    .operations = operations,
    .operation_count = sizeof(operations) / sizeof(*operations),
};

static bool get_regex(GglBuffer pattern, GglByteVec *regex) {
    GglError ret = ggl_byte_vec_push(regex, '^');
    if (ret != GGL_ERR_OK) {
        return false;
    }
    bool in_escape = false;
    for (size_t i = 0; i < pattern.len; i++) {
        uint8_t c = pattern.data[i];
        if (in_escape) {
            if (c == (uint8_t) '}') {
                in_escape = false;
                continue;
            }
        } else {
            if (c == (uint8_t) '*') {
                ret = ggl_byte_vec_append(regex, GGL_STR(".*"));
                if (ret != GGL_ERR_OK) {
                    return false;
                }
                continue;
            }
            if ((c == (uint8_t) '$') && (i < pattern.len - 1)
                && (pattern.data[i + 1] == (uint8_t) '{')) {
                in_escape = true;
                i += 1;
                continue;
            }
        }

        switch ((char) c) {
        case '.':
        case '[':
        case ']':
        case '*':
        case '\\':
            ret = ggl_byte_vec_push(regex, '\\');
            ggl_byte_vec_chain_push(&ret, regex, c);
            break;
        case '+':
            ret = ggl_byte_vec_append(regex, GGL_STR("[^/]*"));
            break;
        case '#':
            ret = ggl_byte_vec_append(regex, GGL_STR(".*"));
            break;
        default:
            ret = ggl_byte_vec_push(regex, c);
        }
        if (ret != GGL_ERR_OK) {
            return false;
        }
    }
    ret = ggl_byte_vec_push(regex, '$');
    ggl_byte_vec_chain_push(&ret, regex, '\0');
    return ret == GGL_ERR_OK;
}

bool ggl_ipc_mqtt_policy_matcher(
    GglBuffer request_resource, GglBuffer policy_resource
) {
    GglByteVec regex_vec = GGL_BYTE_VEC((uint8_t[512]) { 0 });
    bool ret = get_regex(policy_resource, &regex_vec);
    if (!ret) {
        return ret;
    }

    regex_t regex;
    int err = regcomp(&regex, (char *) regex_vec.buf.data, REG_NOSUB);
    if (err != 0) {
        GGL_LOGE(
            "mqttproxy", "Failed to compile regex: %s", regex_vec.buf.data
        );
        return false;
    }
    err = regexec(
        &regex,
        (char *) request_resource.data,
        0,
        &(regmatch_t) { .rm_so = 0, .rm_eo = (regoff_t) request_resource.len },
        REG_STARTEND
    );
    regfree(&regex);
    return err == 0;
}
