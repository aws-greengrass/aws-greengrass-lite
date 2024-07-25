/* gravel - Utilities for AWS IoT Core clients
* Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
* SPDX-License-Identifier: Apache-2.0
*/

#include "args.h"
#include "fleet_status_service.h"
#include "ggl/client.h"
#include "ggl/log.h"
#include <string.h>

FssdArgs *args;
static const char TOPIC_START[] = "$aws/things/";
static const char TOPIC_END[] = "/greengrassv2/health/json";
#define TOPIC_PREFIX "$aws/things/"
#define TOPIC_PREFIX_LEN (sizeof(TOPIC_PREFIX) - 1)
#define TOPIC_SUFFIX "/greengrassv2/health/json"
#define TOPIC_SUFFIX_LEN (sizeof(TOPIC_SUFFIX) - 1)
#define MAX_THING_NAME_LEN 128
#define TOPIC_BUFFER_LEN (TOPIC_PREFIX_LEN + MAX_THING_NAME_LEN + TOPIC_SUFFIX_LEN)

static const char PAYLOAD_START[] = "{\"ggcVersion\":\"2.13.0\",\"platform\":\"linux\",\"architecture\":\"amd64\",\"thing\":\"";
static const char PAYLOAD_END[] = "\",\"sequenceNumber\":1,\"timestamp\":10,\"messageType\":\"COMPLETE\",\"trigger\":\"NUCLEUS_LAUNCH\",\"overallDeviceStatus\":\"HEALTHY\",\"components\":[]}";
#define PAYLOAD_PREFIX "{\"ggcVersion\":\"2.13.0\",\"platform\":\"linux\",\"architecture\":\"amd64\",\"thing\":\""
#define PAYLOAD_PREFIX_LEN (sizeof(PAYLOAD_PREFIX) - 1)
#define PAYLOAD_SUFFIX "\",\"sequenceNumber\":1,\"timestamp\":10,\"messageType\":\"COMPLETE\",\"trigger\":\"NUCLEUS_LAUNCH\",\"overallDeviceStatus\":\"HEALTHY\",\"components\":[]}"
#define PAYLOAD_SUFFIX_LEN (sizeof(PAYLOAD_SUFFIX) - 1)
#define PAYLOAD_BUFFER_LEN (PAYLOAD_PREFIX_LEN + MAX_THING_NAME_LEN + PAYLOAD_SUFFIX_LEN)

GglError publish_message(const char* thing_name) {
    // build topic name
    char topic[TOPIC_BUFFER_LEN + 1] = {0};

    if(strlen(thing_name) > MAX_THING_NAME_LEN) {
        GGL_LOGE("fss", "Thing name too long.");
        return GGL_ERR_RANGE;
    }

    strncat(topic, TOPIC_START, TOPIC_PREFIX_LEN);
    strncat(topic, thing_name, MAX_THING_NAME_LEN);
    strncat(topic, TOPIC_END, TOPIC_SUFFIX_LEN);

    GglBuffer topic_name = {
        .data = topic,
        .len = strlen(topic)
    };

    // build payload
    char payload[PAYLOAD_BUFFER_LEN + 1] = {0};
    strncat(payload, PAYLOAD_START, PAYLOAD_PREFIX_LEN);
    strncat(payload, thing_name, MAX_THING_NAME_LEN);
    strncat(payload, PAYLOAD_END, PAYLOAD_SUFFIX_LEN);

    GglBuffer msg = {
        .data = payload,
        .len = strlen(payload)
    };

    GglError err = ggl_notify(
        GGL_STR("/aws/ggl/iotcored"),
        GGL_STR("publish"),
        GGL_MAP(
            {GGL_STR("topic"), GGL_OBJ(topic_name)},
            {GGL_STR("payload"), GGL_OBJ(msg)})
    );
    if (err == GGL_ERR_OK) {
        GGL_LOGI("Fleet Status Service", "Published update");
    }
    return err;
}
