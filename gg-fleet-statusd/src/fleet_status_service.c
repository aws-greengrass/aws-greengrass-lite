// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "fleet_status_service.h"
#include <sys/types.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_THING_NAME_LEN 128

#define TOPIC_PREFIX "$aws/things/"
#define TOPIC_PREFIX_LEN (sizeof(TOPIC_PREFIX) - 1)
#define TOPIC_SUFFIX "/greengrassv2/health/json"
#define TOPIC_SUFFIX_LEN (sizeof(TOPIC_SUFFIX) - 1)

#define TOPIC_BUFFER_LEN \
    (TOPIC_PREFIX_LEN + MAX_THING_NAME_LEN + TOPIC_SUFFIX_LEN)

#define PAYLOAD_BUFFER_LEN 5000

GglError publish_message(GglBuffer thing_name) {
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mtx);
    GGL_DEFER(pthread_mutex_unlock, mtx);

    // build topic name
    if (thing_name.len > MAX_THING_NAME_LEN) {
        GGL_LOGE("fss", "Thing name too long.");
        return GGL_ERR_RANGE;
    }

    static uint8_t topic_buf[TOPIC_BUFFER_LEN];
    GglByteVec topic_vec = GGL_BYTE_VEC(topic_buf);
    GglError ret = ggl_byte_vec_append(&topic_vec, GGL_STR(TOPIC_PREFIX));
    ggl_byte_vec_chain_append(&ret, &topic_vec, thing_name);
    ggl_byte_vec_chain_append(&ret, &topic_vec, GGL_STR(TOPIC_SUFFIX));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglObject payload_obj = GGL_OBJ_MAP(
        { GGL_STR("ggcVersion"), GGL_OBJ_STR("3.13.0") },
        { GGL_STR("platform"), GGL_OBJ_STR("linux") },
        { GGL_STR("architecture"), GGL_OBJ_STR("amd64") },
        { GGL_STR("runtime"), GGL_OBJ_STR("NucleusLite") },
        { GGL_STR("thing"), GGL_OBJ(thing_name) },
        { GGL_STR("sequenceNumber"), GGL_OBJ_I64(1) },
        { GGL_STR("timestamp"), GGL_OBJ_I64(10) },
        { GGL_STR("messageType"), GGL_OBJ_STR("COMPLETE") },
        { GGL_STR("trigger"), GGL_OBJ_STR("NUCLEUS_LAUNCH") },
        { GGL_STR("overallDeviceStatus"), GGL_OBJ_STR("HEALTHY") },
        { GGL_STR("components"), GGL_OBJ_LIST() }
    );

    // build payload
    static uint8_t payload_buf[PAYLOAD_BUFFER_LEN];
    GglBuffer payload = GGL_BUF(payload_buf);
    ret = ggl_json_encode(payload_obj, &payload);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_aws_iot_mqtt_publish(topic_vec.buf, payload, 0, false);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_LOGI("Fleet Status Service", "Published update");
    return GGL_ERR_OK;
}
