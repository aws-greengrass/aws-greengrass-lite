// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "fleet_status_service.h"
#include <sys/types.h>
#include <ggl/core_bus/client.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <pthread.h>
#include <stdint.h>

#define MAX_THING_NAME_LEN 128

#define TOPIC_PREFIX "$aws/things/"
#define TOPIC_PREFIX_LEN (sizeof(TOPIC_PREFIX) - 1)
#define TOPIC_SUFFIX "/greengrassv2/health/json"
#define TOPIC_SUFFIX_LEN (sizeof(TOPIC_SUFFIX) - 1)

#define TOPIC_BUFFER_LEN \
    (TOPIC_PREFIX_LEN + MAX_THING_NAME_LEN + TOPIC_SUFFIX_LEN)

#define PAYLOAD_PREFIX \
    "{\"ggcVersion\":\"2.13.0\",\"platform\":\"linux\",\"architecture\":" \
    "\"amd64\",\"runtime\":\"NucleusLite\",\"thing\":\""
#define PAYLOAD_PREFIX_LEN (sizeof(PAYLOAD_PREFIX) - 1)
#define PAYLOAD_SUFFIX \
    "\",\"sequenceNumber\":1,\"timestamp\":10,\"messageType\":\"COMPLETE\"," \
    "\"trigger\":\"NUCLEUS_LAUNCH\",\"overallDeviceStatus\":\"HEALTHY\"," \
    "\"components\":[]}"
#define PAYLOAD_SUFFIX_LEN (sizeof(PAYLOAD_SUFFIX) - 1)

#define PAYLOAD_BUFFER_LEN \
    (PAYLOAD_PREFIX_LEN + MAX_THING_NAME_LEN + PAYLOAD_SUFFIX_LEN)

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

    // build payload
    static uint8_t payload_buf[PAYLOAD_BUFFER_LEN];
    GglByteVec payload_vec = GGL_BYTE_VEC(payload_buf);
    ret = ggl_byte_vec_append(&topic_vec, GGL_STR(PAYLOAD_PREFIX));
    ggl_byte_vec_chain_append(&ret, &topic_vec, thing_name);
    ggl_byte_vec_chain_append(&ret, &topic_vec, GGL_STR(PAYLOAD_SUFFIX));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_notify(
        GGL_STR("aws_iot_mqtt"),
        GGL_STR("publish"),
        GGL_MAP(
            { GGL_STR("topic"), GGL_OBJ(topic_vec.buf) },
            { GGL_STR("payload"), GGL_OBJ(payload_vec.buf) }
        )
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_LOGI("Fleet Status Service", "Published update");
    return GGL_ERR_OK;
}
