// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "fleet_status_service.h"
#include <assert.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/json_encode.h>
#include <gg/list.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/vector.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/core_bus/gg_healthd.h>
#include <ggl/nucleus/constants.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
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

static const GgBuffer ARCHITECTURE =
#if defined(__x86_64__)
    GG_STR("amd64");
#elif defined(__i386__)
    GG_STR("x86");
#elif defined(__aarch64__)
    GG_STR("aarch64");
#elif defined(__arm__)
    GG_STR("arm");
#else
#error "Unknown target architecture"
    { 0 };
#endif

// TODO: Split this function up
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
GgError publish_fleet_status_update(
    GgBuffer thing_name, GgBuffer trigger, GgMap deployment_info
) {
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    GG_MTX_SCOPE_GUARD(&mtx);

    bool device_healthy = true;

    // The size of the payload buffer minus some bytes we will need for
    // boilerplate contents, is the max we can send
    GgBuffer component_info_mem
        = GG_BUF((uint8_t[PAYLOAD_BUFFER_LEN - 128]) { 0 });
    GgArena alloc = gg_arena_init(component_info_mem);

    // retrieve running components from services config
    GgList components;
    GgError ret = ggl_gg_config_list(
        GG_BUF_LIST(GG_STR("services")), &alloc, &components
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Unable to retrieve list of components from config with error %s",
            gg_strerror(ret)
        );
        return ret;
    }

    // get status for each running component
    GgKV component_infos[GGL_MAX_GENERIC_COMPONENTS][5];
    GgObjVec component_statuses
        = GG_OBJ_VEC((GgObject[GGL_MAX_GENERIC_COMPONENTS]) { 0 });
    size_t component_count = 0;
    GG_LIST_FOREACH (component_obj, components) {
        if (gg_obj_type(*component_obj) != GG_TYPE_BUF) {
            GG_LOGE(
                "Incorrect type of component key received. Expected buffer. Cannot publish fleet status update for this entry."
            );
            continue;
        }
        GgBuffer component = gg_obj_into_buf(*component_obj);

        // ignore core components for now, gghealthd does not support
        // getting their health yet
        GgBufList ignored_components = GG_BUF_LIST(
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("aws.greengrass.fleet_provisioning"),
            GG_STR("DeploymentService"),
            GG_STR("FleetStatusService"),
            GG_STR("main"),
            GG_STR("TelemetryAgent"),
            GG_STR("UpdateSystemPolicyService")
        );
        bool ignore_component = false;
        GG_BUF_LIST_FOREACH (ignored_component, ignored_components) {
            if (gg_buffer_eq(*ignored_component, component)) {
                ignore_component = true;
                break;
            }
        }
        if (ignore_component) {
            continue;
        }

        // retrieve component version from config
        GgBuffer version_resp;
        ret = ggl_gg_config_read_str(
            GG_BUF_LIST(GG_STR("services"), component, GG_STR("version")),
            &alloc,
            &version_resp
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Unable to retrieve version of %.*s with error %s. Cannot publish fleet status update for this component.",
                (int) component.len,
                component.data,
                gg_strerror(ret)
            );
            continue;
        }

        // retrieve component health status
        uint8_t component_health_arr[NAME_MAX];
        GgBuffer component_health = GG_BUF(component_health_arr);
        ret = ggl_gghealthd_retrieve_component_status(
            component, &alloc, &component_health
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Failed to retrieve health status for %.*s with error %s. Cannot publish fleet status update for this component.",
                (int) component.len,
                component.data,
                gg_strerror(ret)
            );
            continue;
        }

        // if a component is broken, mark the device as unhealthy
        if (gg_buffer_eq(component_health, GG_STR("BROKEN"))) {
            device_healthy = false;
        }

        // retrieve fleet config arn list from config
        GgObject arn_list;
        ret = ggl_gg_config_read(
            GG_BUF_LIST(GG_STR("services"), component, GG_STR("configArn")),
            &alloc,
            &arn_list
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Unable to retrieve fleet configuration arn list for component %.*s from config with error %s. Cannot publish fleet status update for this component.",
                (int) component.len,
                component.data,
                gg_strerror(ret)
            );
            continue;
        }
        if (gg_obj_type(arn_list) != GG_TYPE_LIST) {
            GG_LOGE(
                "Fleet configuration arn retrieved from config not of type list for component %.*s. Cannot publish fleet status update for this component.",
                (int) component.len,
                component.data
            );
            continue;
        }

        // building component info to be in line with the cloud's expected pojo
        // format
        GgMap component_info = GG_MAP(
            gg_kv(GG_STR("componentName"), gg_obj_buf(component)),
            gg_kv(GG_STR("version"), gg_obj_buf(version_resp)),
            gg_kv(GG_STR("fleetConfigArns"), arn_list),
            gg_kv(GG_STR("isRoot"), gg_obj_bool(true)),
            gg_kv(GG_STR("status"), gg_obj_buf(component_health))
        );

        memcpy(
            component_infos[component_count],
            component_info.pairs,
            sizeof(component_infos[component_count])
        );
        component_info.pairs = component_infos[component_count];

        // store component info
        ret = gg_obj_vec_push(&component_statuses, gg_obj_map(component_info));
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Failed to add component info for %.*s to component list with error %s. Cannot publish fleet status update for this component.",
                (int) component.len,
                component.data,
                gg_strerror(ret)
            );
            continue;
        }

        component_count++;
    }
    assert(component_count == component_statuses.list.len);

    GgBuffer overall_device_status;
    if (device_healthy) {
        overall_device_status = GG_STR("HEALTHY");
    } else {
        overall_device_status = GG_STR("UNHEALTHY");
    }

    int64_t timestamp;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    timestamp = (int64_t) now.tv_sec * 1000 + now.tv_nsec / 1000000;

    // build topic name
    if (thing_name.len > MAX_THING_NAME_LEN) {
        GG_LOGE("Thing name too long.");
        return GG_ERR_RANGE;
    }

    static uint8_t topic_buf[TOPIC_BUFFER_LEN];
    GgByteVec topic_vec = GG_BYTE_VEC(topic_buf);
    ret = gg_byte_vec_append(&topic_vec, GG_STR(TOPIC_PREFIX));
    gg_byte_vec_chain_append(&ret, &topic_vec, thing_name);
    gg_byte_vec_chain_append(&ret, &topic_vec, GG_STR(TOPIC_SUFFIX));
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // check for a persisted sequence number
    GgObject sequence_obj;
    ret = ggl_gg_config_read(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("FleetStatusService"),
            GG_STR("sequenceNumber")
        ),
        &alloc,
        &sequence_obj
    );
    int64_t sequence = 1;
    if ((ret == GG_ERR_OK) && (gg_obj_type(sequence_obj) == GG_TYPE_I64)) {
        // if sequence number found, increment it
        sequence = gg_obj_into_i64(sequence_obj) + 1;
    }
    // set the current sequence number in the config
    ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("FleetStatusService"),
            GG_STR("sequenceNumber")
        ),
        gg_obj_i64(sequence),
        &(int64_t) { 0 }
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to write sequence number to configuration.");
        return ret;
    }

    GgObject payload_obj = gg_obj_map(GG_MAP(
        gg_kv(GG_STR("ggcVersion"), gg_obj_buf(GG_STR(GGL_VERSION))),
        gg_kv(GG_STR("platform"), gg_obj_buf(GG_STR("linux"))),
        gg_kv(GG_STR("architecture"), gg_obj_buf(ARCHITECTURE)),
        gg_kv(GG_STR("runtime"), gg_obj_buf(GG_STR("aws_nucleus_lite"))),
        gg_kv(GG_STR("thing"), gg_obj_buf(thing_name)),
        gg_kv(GG_STR("sequenceNumber"), gg_obj_i64(sequence)),
        gg_kv(GG_STR("timestamp"), gg_obj_i64(timestamp)),
        gg_kv(GG_STR("messageType"), gg_obj_buf(GG_STR("COMPLETE"))),
        gg_kv(GG_STR("trigger"), gg_obj_buf(trigger)),
        gg_kv(GG_STR("overallDeviceStatus"), gg_obj_buf(overall_device_status)),
        gg_kv(GG_STR("components"), gg_obj_list(component_statuses.list)),
        gg_kv(GG_STR("deploymentInformation"), gg_obj_map(deployment_info))
    ));

    // build payload
    static uint8_t payload_buf[PAYLOAD_BUFFER_LEN];
    GgByteVec payload = GG_BYTE_VEC(payload_buf);
    ret = gg_json_encode(payload_obj, gg_byte_vec_writer(&payload));
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = ggl_aws_iot_mqtt_publish(
        GG_STR("aws_iot_mqtt"), topic_vec.buf, payload.buf, 0, false
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GG_LOGI("Published update.");
    return GG_ERR_OK;
}
