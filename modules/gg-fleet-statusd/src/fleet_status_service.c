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
#include <ggl/core_bus/gg_config.h>
#include <ggl/core_bus/gg_healthd.h>
#include <ggl/error.h>
#include <ggl/json_encode.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <ggl/version.h>
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
    GGL_LIST_FOREACH(component_obj, components) {
        if (ggl_obj_type(*component_obj) != GGL_TYPE_BUF) {
            GGL_LOGE(
                "Incorrect type of component key received. Expected buffer. "
                "Cannot publish fleet status update for this entry."
            );
            continue;
        }
        GglBuffer component = ggl_obj_into_buf(*component_obj);

        // ignore core components for now, gghealthd does not support
        // getting their health yet
        GglBufList ignored_components = GGL_BUF_LIST(
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("aws.greengrass.fleet_provisioning"),
            GGL_STR("DeploymentService"),
            GGL_STR("FleetStatusService"),
            GGL_STR("main"),
            GGL_STR("TelemetryAgent"),
            GGL_STR("UpdateSystemPolicyService")
        );
        bool ignore_component = false;
        GGL_BUF_LIST_FOREACH(ignored_component, ignored_components) {
            if (ggl_buffer_eq(*ignored_component, component)) {
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
            GGL_BUF_LIST(GGL_STR("services"), component, GGL_STR("version")),
            &version_resp
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Unable to retrieve version of %.*s with error %s. Cannot "
                "publish fleet "
                "status update for this component.",
                (int) component.len,
                component.data,
                ggl_strerror(ret)
            );
            continue;
        }
        ret = ggl_buf_clone(version_resp, &balloc.alloc, &version_resp);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to copy version response buffer for %.*s with error "
                "%s. Cannot publish fleet status update for this component.",
                (int) component.len,
                component.data,
                ggl_strerror(ret)
            );
            continue;
        }

        // retrieve component health status
        uint8_t component_health_arr[NAME_MAX];
        GglBuffer component_health = GGL_BUF(component_health_arr);
        ret = ggl_gghealthd_retrieve_component_status(
            component, &component_health
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to retrieve health status for %.*s with error %s. "
                "Cannot publish fleet status update for this component.",
                (int) component.len,
                component.data,
                ggl_strerror(ret)
            );
            continue;
        }
        ret = ggl_buf_clone(component_health, &balloc.alloc, &component_health);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to copy component health buffer for %.*s with error "
                "%s. Cannot publish fleet status update for this component.",
                (int) component.len,
                component.data,
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
            GGL_BUF_LIST(GGL_STR("services"), component, GGL_STR("configArn")),
            &balloc.alloc,
            &arn_list
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Unable to retrieve fleet configuration arn list for component "
                "%.*s from "
                "config with error %s. Cannot publish fleet status update for "
                "this component.",
                (int) component.len,
                component.data,
                ggl_strerror(ret)
            );
            continue;
        }
        if (ggl_obj_type(arn_list) != GGL_TYPE_LIST) {
            GGL_LOGE(
                "Fleet configuration arn retrieved from config not of "
                "type list for component %.*s. Cannot publish fleet "
                "status update for this component.",
                (int) component.len,
                component.data
            );
            continue;
        }

        // building component info to be in line with the cloud's expected pojo
        // format
        GglMap component_info = GGL_MAP(
            { GGL_STR("componentName"), ggl_obj_buf(component) },
            { GGL_STR("version"), ggl_obj_buf(version_resp) },
            { GGL_STR("fleetConfigArns"), arn_list },
            { GGL_STR("isRoot"), ggl_obj_bool(true) },
            { GGL_STR("status"), ggl_obj_buf(component_health) }
        );

        memcpy(
            component_infos[component_count],
            component_info.pairs,
            sizeof(component_infos[component_count])
        );
        component_info.pairs = component_infos[component_count];

        // store component info
        ret = ggl_obj_vec_push(
            &component_statuses, ggl_obj_map(component_info)
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to add component info for %.*s to component list with "
                "error %s. Cannot publish fleet status update for this "
                "component.",
                (int) component.len,
                component.data,
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
    GglObject sequence_obj;
    ret = ggl_gg_config_read(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("fleetStatusSequenceNum")),
        &balloc.alloc,
        &sequence_obj
    );
    int64_t sequence = 1;
    if ((ret == GGL_ERR_OK) && (ggl_obj_type(sequence_obj) == GGL_TYPE_I64)) {
        // if sequence number found, increment it
        sequence = ggl_obj_into_i64(sequence_obj) + 1;
    }
    // set the current sequence number in the config
    ret = ggl_gg_config_write(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("fleetStatusSequenceNum")),
        ggl_obj_i64(sequence),
        &(int64_t) { 0 }
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to write sequence number to configuration.");
        return ret;
    }

    GglObject payload_obj = ggl_obj_map(GGL_MAP(
        { GGL_STR("ggcVersion"), ggl_obj_buf(GGL_STR(GGL_VERSION)) },
        { GGL_STR("platform"), ggl_obj_buf(GGL_STR("linux")) },
        { GGL_STR("architecture"), ggl_obj_buf(ARCHITECTURE) },
        { GGL_STR("runtime"), ggl_obj_buf(GGL_STR("aws_nucleus_lite")) },
        { GGL_STR("thing"), ggl_obj_buf(thing_name) },
        { GGL_STR("sequenceNumber"), ggl_obj_i64(sequence) },
        { GGL_STR("timestamp"), ggl_obj_i64(timestamp) },
        { GGL_STR("messageType"), ggl_obj_buf(GGL_STR("COMPLETE")) },
        { GGL_STR("trigger"), ggl_obj_buf(trigger) },
        { GGL_STR("overallDeviceStatus"), ggl_obj_buf(overall_device_status) },
        { GGL_STR("components"), ggl_obj_list(component_statuses.list) },
        { GGL_STR("deploymentInformation"), ggl_obj_map(deployment_info) }
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
