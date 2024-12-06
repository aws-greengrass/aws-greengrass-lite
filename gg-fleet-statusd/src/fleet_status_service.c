// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "fleet_status_service.h"
#include <sys/types.h>
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/cleanup.h>
#include <ggl/constants.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/core_bus/gg_healthd.h>
#include <ggl/error.h>
#include <ggl/json_encode.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>

#define TOPIC_PREFIX "$aws/things/"
#define TOPIC_PREFIX_LEN (sizeof(TOPIC_PREFIX) - 1)
#define TOPIC_SUFFIX "/greengrassv2/health/json"
#define TOPIC_SUFFIX_LEN (sizeof(TOPIC_SUFFIX) - 1)

#define TOPIC_BUFFER_LEN \
    (TOPIC_PREFIX_LEN + MAX_THING_NAME_LEN + TOPIC_SUFFIX_LEN)

#define PAYLOAD_BUFFER_LEN 5000

static const GglBuffer ARCHITECTURE =
#if defined(__x86_64__)
    GGL_STR("amd64");
#elif defined(__i386__)
    GGL_STR("x86");
#elif defined(__aarch64__)
    GGL_STR("aarch64");
#elif defined(__arm__)
    GGL_STR("arm");
#else
#error "Unknown target architecture"
    { 0 };
#endif

// TODO: Split this function up
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
GglError publish_fleet_status_update(
    GglBuffer thing_name, GglBuffer trigger, GglMap deployment_info
) {
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    GGL_MTX_SCOPE_GUARD(&mtx);

    bool device_healthy = true;

    GglBuffer component_info_mem = GGL_BUF((uint8_t[PAYLOAD_BUFFER_LEN - 128]
    ) { 0 }); // The size of the payload buffer minus some bytes we will need
              // for boilerplate contents, is the max we can send
    GglBumpAlloc balloc = ggl_bump_alloc_init(component_info_mem);

    // retrieve running components from services config
    GglList components;
    GglError ret = ggl_gg_config_list(
        GGL_BUF_LIST(GGL_STR("services")), &balloc.alloc, &components
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Unable to retrieve list of components from config with error %s",
            ggl_strerror(ret)
        );
        return ret;
    }

    // get status for each running component
    GglKV component_infos[MAX_COMPONENTS][5];
    GglObjVec component_statuses
        = GGL_OBJ_VEC((GglObject[MAX_COMPONENTS]) { 0 });
    size_t component_count = 0;
    GGL_LIST_FOREACH(component, components) {
        if (component->type != GGL_TYPE_BUF) {
            GGL_LOGE(
                "Incorrect type of component key received. Expected buffer. "
                "Cannot publish fleet status update for this entry."
            );
            continue;
        }

        // ignore core components for now, gghealthd does not support
        // getting their health yet
        GglList ignored_components = GGL_LIST(
            GGL_OBJ_BUF(GGL_STR("aws.greengrass.NucleusLite")),
            GGL_OBJ_BUF(GGL_STR("aws.greengrass.fleet_provisioning")),
            GGL_OBJ_BUF(GGL_STR("DeploymentService")),
            GGL_OBJ_BUF(GGL_STR("FleetStatusService")),
            GGL_OBJ_BUF(GGL_STR("main")),
            GGL_OBJ_BUF(GGL_STR("TelemetryAgent")),
            GGL_OBJ_BUF(GGL_STR("UpdateSystemPolicyService"))
        );
        bool ignore_component = false;
        GGL_LIST_FOREACH(ignored_component, ignored_components) {
            if (ignored_component->type != GGL_TYPE_BUF) {
                GGL_LOGE(
                    "Expected type buffer for ignored component list, but got "
                    "something else. Unable to match this ignored component."
                );
                continue;
            }
            if (ggl_buffer_eq(ignored_component->buf, component->buf)) {
                ignore_component = true;
                break;
            }
        }
        if (ignore_component) {
            continue;
        }

        // retrieve component version from config
        static uint8_t version_resp_mem[128] = { 0 };
        GglBuffer version_resp = GGL_BUF(version_resp_mem);
        ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"), component->buf, GGL_STR("version")
            ),
            &version_resp
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Unable to retrieve version of %.*s with error %s. Cannot "
                "publish fleet "
                "status update for this component.",
                (int) component->buf.len,
                component->buf.data,
                ggl_strerror(ret)
            );
            continue;
        }
        ret = ggl_buf_clone(version_resp, &balloc.alloc, &version_resp);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to copy version response buffer for %.*s with error "
                "%s. Cannot publish fleet status update for this component.",
                (int) component->buf.len,
                component->buf.data,
                ggl_strerror(ret)
            );
            continue;
        }

        // retrieve component health status
        uint8_t component_health_arr[NAME_MAX];
        GglBuffer component_health = GGL_BUF(component_health_arr);
        ret = ggl_gghealthd_retrieve_component_status(
            component->buf, &component_health
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to retrieve health status for %.*s with error %s. "
                "Cannot publish fleet status update for this component.",
                (int) component->buf.len,
                component->buf.data,
                ggl_strerror(ret)
            );
            continue;
        }
        ret = ggl_buf_clone(component_health, &balloc.alloc, &component_health);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to copy component health buffer for %.*s with error "
                "%s. Cannot publish fleet status update for this component.",
                (int) component->buf.len,
                component->buf.data,
                ggl_strerror(ret)
            );
            continue;
        }

        // if a component is broken, mark the device as unhealthy
        if (ggl_buffer_eq(component_health, GGL_STR("BROKEN"))) {
            device_healthy = false;
        }

        // retrieve fleet config arn list from config
        GglObject arn_list;
        ret = ggl_gg_config_read(
            GGL_BUF_LIST(
                GGL_STR("services"), component->buf, GGL_STR("configArn")
            ),
            &balloc.alloc,
            &arn_list
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Unable to retrieve fleet configuration arn list for component "
                "%.*s from "
                "config with error %s. Cannot publish fleet status update for "
                "this component.",
                (int) component->buf.len,
                component->buf.data,
                ggl_strerror(ret)
            );
            continue;
        }
        if (arn_list.type != GGL_TYPE_LIST) {
            GGL_LOGE(
                "Fleet configuration arn retrieved from config not of "
                "type list for component %.*s. Cannot publish fleet "
                "status update for this component.",
                (int) component->buf.len,
                component->buf.data
            );
            continue;
        }

        // building component info to be in line with the cloud's expected pojo
        // format
        GglObject component_info = GGL_OBJ_MAP(GGL_MAP(
            { GGL_STR("componentName"), GGL_OBJ_BUF(component->buf) },
            { GGL_STR("version"), GGL_OBJ_BUF(version_resp) },
            { GGL_STR("fleetConfigArns"), arn_list },
            { GGL_STR("isRoot"), GGL_OBJ_BOOL(true) },
            { GGL_STR("status"), GGL_OBJ_BUF(component_health) }
        ));

        memcpy(
            component_infos[component_count],
            component_info.map.pairs,
            sizeof(component_infos[component_count])
        );
        component_info.map.pairs = component_infos[component_count];

        // store component info
        ret = ggl_obj_vec_push(&component_statuses, component_info);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to add component info for %.*s to component list with "
                "error %s. Cannot publish fleet status update for this "
                "component.",
                (int) component->buf.len,
                component->buf.data,
                ggl_strerror(ret)
            );
            continue;
        }

        component_count++;
    }
    assert(component_count == component_statuses.list.len);

    GglBuffer overall_device_status;
    if (device_healthy) {
        overall_device_status = GGL_STR("HEALTHY");
    } else {
        overall_device_status = GGL_STR("UNHEALTHY");
    }

    int64_t timestamp;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    timestamp = (int64_t) now.tv_sec * 1000 + now.tv_nsec / 1000000;

    // build topic name
    if (thing_name.len > MAX_THING_NAME_LEN) {
        GGL_LOGE("Thing name too long.");
        return GGL_ERR_RANGE;
    }

    static uint8_t topic_buf[TOPIC_BUFFER_LEN];
    GglByteVec topic_vec = GGL_BYTE_VEC(topic_buf);
    ret = ggl_byte_vec_append(&topic_vec, GGL_STR(TOPIC_PREFIX));
    ggl_byte_vec_chain_append(&ret, &topic_vec, thing_name);
    ggl_byte_vec_chain_append(&ret, &topic_vec, GGL_STR(TOPIC_SUFFIX));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // check for a persisted sequence number
    GglObject sequence;
    ret = ggl_gg_config_read(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("fleetStatusSequenceNum")),
        &balloc.alloc,
        &sequence
    );
    if (ret == GGL_ERR_OK && sequence.type == GGL_TYPE_I64) {
        // if sequence number found, increment it
        sequence.i64 += 1;
    } else {
        // if sequence number not found, set it
        sequence = GGL_OBJ_I64(1);
    }
    // set the current sequence number in the config
    ret = ggl_gg_config_write(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("fleetStatusSequenceNum")),
        sequence,
        &(int64_t) { 0 }
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to write sequence number to configuration.");
        return ret;
    }

    GglObject payload_obj = GGL_OBJ_MAP(GGL_MAP(
        { GGL_STR("ggcVersion"), GGL_OBJ_BUF(GGL_STR("1.0.0")) },
        { GGL_STR("platform"), GGL_OBJ_BUF(GGL_STR("linux")) },
        { GGL_STR("architecture"), GGL_OBJ_BUF(ARCHITECTURE) },
        { GGL_STR("runtime"), GGL_OBJ_BUF(GGL_STR("aws_nucleus_lite")) },
        { GGL_STR("thing"), GGL_OBJ_BUF(thing_name) },
        { GGL_STR("sequenceNumber"), sequence },
        { GGL_STR("timestamp"), GGL_OBJ_I64(timestamp) },
        { GGL_STR("messageType"), GGL_OBJ_BUF(GGL_STR("COMPLETE")) },
        { GGL_STR("trigger"), GGL_OBJ_BUF(trigger) },
        { GGL_STR("overallDeviceStatus"), GGL_OBJ_BUF(overall_device_status) },
        { GGL_STR("components"), GGL_OBJ_LIST(component_statuses.list) },
        { GGL_STR("deploymentInformation"), GGL_OBJ_MAP(deployment_info) }
    ));

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

    GGL_LOGI("Published update.");
    return GGL_ERR_OK;
}
