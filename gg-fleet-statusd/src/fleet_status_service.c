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
#include <pthread.h>
#include <string.h>
#include <stdint.h>

static const char TOPIC_START[] = "$aws/things/";
static const char TOPIC_END[] = "/greengrassv2/health/json";
#define TOPIC_PREFIX "$aws/things/"
#define TOPIC_PREFIX_LEN (sizeof(TOPIC_PREFIX) - 1)
#define TOPIC_SUFFIX "/greengrassv2/health/json"
#define TOPIC_SUFFIX_LEN (sizeof(TOPIC_SUFFIX) - 1)
#define MAX_THING_NAME_LEN 128
#define TOPIC_BUFFER_LEN \
    (TOPIC_PREFIX_LEN + MAX_THING_NAME_LEN + TOPIC_SUFFIX_LEN)

static const char PAYLOAD_START[]
    = "{\"ggcVersion\":\"2.13.0\",\"platform\":\"linux\",\"architecture\":"
      "\"amd64\",\"runtime\":\"NucleusLite\",\"thing\":\"";
static const char PAYLOAD_END[]
    = "\",\"sequenceNumber\":1,\"timestamp\":10,\"messageType\":\"COMPLETE\","
      "\"trigger\":\"NUCLEUS_LAUNCH\",\"overallDeviceStatus\":\"HEALTHY\","
      "\"components\":[]}";
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
    static char topic[TOPIC_BUFFER_LEN + 1] = { 0 };

    if (thing_name.len > MAX_THING_NAME_LEN) {
        GGL_LOGE("fss", "Thing name too long.");
        return GGL_ERR_RANGE;
    }

    strncat(topic, TOPIC_START, TOPIC_PREFIX_LEN);
    strncat(topic, (char *) thing_name.data, MAX_THING_NAME_LEN);
    strncat(topic, TOPIC_END, TOPIC_SUFFIX_LEN);

    GglBuffer topic_name = { .data = (uint8_t *) topic, .len = strlen(topic) };

    // build payload
    static char payload[PAYLOAD_BUFFER_LEN + 1] = { 0 };
    strncat(payload, PAYLOAD_START, PAYLOAD_PREFIX_LEN);
    strncat(payload, (char *) thing_name.data, MAX_THING_NAME_LEN);
    strncat(payload, PAYLOAD_END, PAYLOAD_SUFFIX_LEN);

    GglBuffer msg = { .data = (uint8_t *) payload, .len = strlen(payload) };

    GglError err = ggl_notify(
        GGL_STR("aws_iot_mqtt"),
        GGL_STR("publish"),
        GGL_MAP(
            { GGL_STR("topic"), GGL_OBJ(topic_name) },
            { GGL_STR("payload"), GGL_OBJ(msg) }
        )
    );
    if (err == GGL_ERR_OK) {
        GGL_LOGI("Fleet Status Service", "Published update");
    }
    return err;
}
