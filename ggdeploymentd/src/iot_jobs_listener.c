// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "iot_jobs_listener.h"
#include "deployment_queue.h"
#include <sys/types.h>
#include <ggl/alloc.h>
#include <ggl/aws_iot_call.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/cleanup.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/json_decode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/utils.h>
#include <ggl/vector.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

#define MAX_THING_NAME_LEN 128

typedef enum QualityOfService {
    QOS_FIRE_AND_FORGET = 0,
    QOS_AT_LEAST_ONCE = 1,
    QOS_EXACTLY_ONCE = 2
} QoS;

typedef enum DeploymentStatusAction {
    DSA_DO_NOTHING = 0,
    DSA_ENQUEUE_JOB = 1,
    DSA_CANCEL_JOB = 2,
} DeploymentStatusAction;

// format strings for greengrass deployment job topic filters
#define THINGS_TOPIC_PREFIX "$aws/things/"
#define JOBS_TOPIC_PREFIX "/jobs/"
#define JOBS_UPDATE_TOPIC "/namespace-aws-gg-deployment/update"
#define JOBS_GET_TOPIC "/namespace-aws-gg-deployment/get"
#define NEXT_JOB_EXECUTION_CHANGED_TOPIC \
    "/jobs/notify-next-namespace-aws-gg-deployment"

#define NEXT_JOB_LITERAL "$next"

// TODO: remove when adding backoff algorithm
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

static uint8_t thing_name_mem[MAX_THING_NAME_LEN];
static GglBuffer thing_name_buf;

static uint8_t current_job_id_buf[64];
static GglByteVec current_job_id;
static uint8_t current_deployment_id_buf[64];
static GglByteVec current_deployment_id;
static int64_t current_job_version;

pthread_mutex_t topic_scratch_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint8_t topic_scratch[256];
static uint8_t response_scratch[4096];
static uint8_t subscription_scratch[4096];

// aws_iot_mqtt subscription handles
static uint32_t next_job_handle;

static GglError create_get_next_job_topic(
    GglBuffer thing_name, GglBuffer *job_topic
) {
    GglByteVec job_topic_vec = ggl_byte_vec_init(*job_topic);
    GglError err = GGL_ERR_OK;
    ggl_byte_vec_chain_append(
        &err, &job_topic_vec, GGL_STR(THINGS_TOPIC_PREFIX)
    );
    ggl_byte_vec_chain_append(&err, &job_topic_vec, thing_name);
    ggl_byte_vec_chain_append(&err, &job_topic_vec, GGL_STR(JOBS_TOPIC_PREFIX));
    ggl_byte_vec_chain_append(&err, &job_topic_vec, GGL_STR(NEXT_JOB_LITERAL));
    ggl_byte_vec_chain_append(&err, &job_topic_vec, GGL_STR(JOBS_GET_TOPIC));
    if (err == GGL_ERR_OK) {
        *job_topic = job_topic_vec.buf;
    }
    return err;
}

static GglError create_update_job_topic(
    GglBuffer thing_name, GglBuffer job_id, GglBuffer *job_topic
) {
    GglByteVec job_topic_vec = ggl_byte_vec_init(*job_topic);
    GglError err = GGL_ERR_OK;
    ggl_byte_vec_chain_append(
        &err, &job_topic_vec, GGL_STR(THINGS_TOPIC_PREFIX)
    );
    ggl_byte_vec_chain_append(&err, &job_topic_vec, thing_name);
    ggl_byte_vec_chain_append(&err, &job_topic_vec, GGL_STR(JOBS_TOPIC_PREFIX));
    ggl_byte_vec_chain_append(&err, &job_topic_vec, job_id);
    ggl_byte_vec_chain_append(&err, &job_topic_vec, GGL_STR(JOBS_UPDATE_TOPIC));
    if (err == GGL_ERR_OK) {
        *job_topic = job_topic_vec.buf;
    }
    return err;
}

static GglError create_next_job_execution_changed_topic(
    GglBuffer thing_name, GglBuffer *job_topic
) {
    GglByteVec job_topic_vec = ggl_byte_vec_init(*job_topic);
    GglError err = GGL_ERR_OK;
    ggl_byte_vec_chain_append(
        &err, &job_topic_vec, GGL_STR(THINGS_TOPIC_PREFIX)
    );
    ggl_byte_vec_chain_append(&err, &job_topic_vec, thing_name);
    ggl_byte_vec_chain_append(
        &err, &job_topic_vec, GGL_STR(NEXT_JOB_EXECUTION_CHANGED_TOPIC)
    );
    if (err == GGL_ERR_OK) {
        *job_topic = job_topic_vec.buf;
    }
    return err;
}

static GglError update_job(
    GglBuffer job_id, GglBuffer job_status, int64_t *version
);
static GglError process_job_execution(GglMap job_execution);

// Retrieve thingName from config
static GglError get_thing_name(void) {
    thing_name_buf = GGL_BUF(thing_name_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("thingName")), &thing_name_buf
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to read thingName from config.");
        return ret;
    }

    return GGL_ERR_OK;
}

// Decode MQTT payload as JSON into GglObject representation
static GglError deserialize_payload(
    GglAlloc *alloc, GglObject data, GglObject *json_object
) {
    GglBuffer *topic;
    GglBuffer *payload;

    GglError ret
        = ggl_aws_iot_mqtt_subscribe_parse_resp(data, &topic, &payload);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_LOGI(
        "Got message from IoT Core; topic: %.*s, payload: %.*s.",
        (int) topic->len,
        topic->data,
        (int) payload->len,
        payload->data
    );

    ret = ggl_json_decode_destructive(*payload, alloc, json_object);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to parse job doc JSON.");
        return ret;
    }
    return GGL_ERR_OK;
}

static GglError update_job(
    GglBuffer job_id, GglBuffer job_status, int64_t *version
) {
    GGL_MTX_SCOPE_GUARD(&topic_scratch_mutex);
    GglBuffer topic = GGL_BUF(topic_scratch);
    GglError ret = create_update_job_topic(thing_name_buf, job_id, &topic);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    uint8_t version_buf[16] = { 0 };
    int len = snprintf(
        (char *) version_buf, sizeof(version_buf), "%" PRIi64, *version
    );
    if (len <= 0) {
        GGL_LOGE("Version too big");
        return GGL_ERR_RANGE;
    }

    // https://docs.aws.amazon.com/iot/latest/developerguide/jobs-mqtt-api.html
    GglObject payload_object = GGL_OBJ_MAP(
        { GGL_STR("status"), GGL_OBJ(job_status) },
        { GGL_STR("expectedVersion"),
          GGL_OBJ((GglBuffer) { .data = version_buf, .len = (size_t) len }) },
        { GGL_STR("clientToken"), GGL_OBJ_STR("jobs-nucleus-lite") }
    );

    GglBumpAlloc call_alloc = ggl_bump_alloc_init(GGL_BUF(response_scratch));
    GglObject result = GGL_OBJ_NULL();
    ggl_aws_iot_call(topic, payload_object, &call_alloc.alloc, &result);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to publish on update job topic");
        return ret;
    }
    ++(*version);
    return GGL_ERR_OK;
}

static GglError describe_next_job(void) {
    GGL_MTX_SCOPE_GUARD(&topic_scratch_mutex);
    GglBuffer topic = GGL_BUF(topic_scratch);
    GglError ret = create_get_next_job_topic(thing_name_buf, &topic);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // https://docs.aws.amazon.com/iot/latest/developerguide/jobs-mqtt-api.html
    GglObject payload_object = GGL_OBJ_MAP(
        { GGL_STR("jobId"), GGL_OBJ_STR(NEXT_JOB_LITERAL) },
        { GGL_STR("thingName"), GGL_OBJ(thing_name_buf) },
        { GGL_STR("includeJobDocument"), GGL_OBJ_BOOL(true) },
        { GGL_STR("clientToken"), GGL_OBJ_STR("jobs-nucleus-lite") }
    );

    GglBumpAlloc call_alloc = ggl_bump_alloc_init(GGL_BUF(response_scratch));
    GglObject job_description = GGL_OBJ_NULL();
    ggl_aws_iot_call(
        topic, payload_object, &call_alloc.alloc, &job_description
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to publish on describe job topic");
        return ret;
    }

    if (job_description.type != GGL_TYPE_MAP) {
        GGL_LOGE("Describe payload not of type Map");
        return GGL_ERR_FAILURE;
    }

    GglObject *execution = NULL;
    ret = ggl_map_validate(
        job_description.map,
        GGL_MAP_SCHEMA({ GGL_STR("execution"), false, GGL_TYPE_MAP, &execution }
        )
    );
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }
    if (execution == NULL) {
        return GGL_ERR_OK;
    }
    return process_job_execution(execution->map);
}

static GglError enqueue_job(GglMap deployment_doc, GglBuffer job_id) {
    // TODO: check if current job is canceled before clobbering
    current_job_version = 1;
    current_job_id = GGL_BYTE_VEC(current_job_id_buf);
    GglError ret = ggl_byte_vec_append(&current_job_id, job_id);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Job ID too long.");
        return ret;
    }

    current_deployment_id = GGL_BYTE_VEC(current_deployment_id_buf);

    // TODO: backoff algorithm
    int64_t retries = 1;
    while ((ret
            = ggl_deployment_enqueue(deployment_doc, &current_deployment_id))
           == GGL_ERR_BUSY) {
        int64_t sleep_for = 1 << MIN(7, retries);
        ggl_sleep(sleep_for);
        ++retries;
    };

    if (ret != GGL_ERR_OK) {
        update_job(job_id, GGL_STR("FAILURE"), &current_job_version);
    }

    return ret;
}

static GglError process_job_execution(GglMap job_execution) {
    GglObject *job_id = NULL;
    GglObject *status = NULL;
    GglObject *deployment_doc = NULL;
    GglError err = ggl_map_validate(
        job_execution,
        GGL_MAP_SCHEMA(
            { GGL_STR("jobId"), false, GGL_TYPE_BUF, &job_id },
            { GGL_STR("status"), false, GGL_TYPE_BUF, &status },
            { GGL_STR("jobDocument"), false, GGL_TYPE_MAP, &deployment_doc }
        )
    );
    if (err != GGL_ERR_OK) {
        GGL_LOGE("Failed to validate job execution response.");
        return GGL_ERR_FAILURE;
    }
    if ((status == NULL) || (job_id == NULL)) {
        return GGL_ERR_OK;
    }
    DeploymentStatusAction action;
    {
        GglMap status_action_map = GGL_MAP(
            { GGL_STR("QUEUED"), GGL_OBJ_I64(DSA_ENQUEUE_JOB) },
            { GGL_STR("IN_PROGRESS"), GGL_OBJ_I64(DSA_ENQUEUE_JOB) },
            { GGL_STR("SUCCEEDED"), GGL_OBJ_I64(DSA_DO_NOTHING) },
            { GGL_STR("FAILED"), GGL_OBJ_I64(DSA_DO_NOTHING) },
            { GGL_STR("TIMED_OUT"), GGL_OBJ_I64(DSA_CANCEL_JOB) },
            { GGL_STR("REJECTED"), GGL_OBJ_I64(DSA_DO_NOTHING) },
            { GGL_STR("REMOVED"), GGL_OBJ_I64(DSA_CANCEL_JOB) },
            { GGL_STR("CANCELED"), GGL_OBJ_I64(DSA_CANCEL_JOB) },
        );
        GglObject *integer = NULL;
        if (!ggl_map_get(status_action_map, status->buf, &integer)) {
            GGL_LOGE("Job status not a valid value");
            return GGL_ERR_INVALID;
        }
        action = (DeploymentStatusAction) integer->i64;
    }
    switch (action) {
    case DSA_CANCEL_JOB:
        // TODO: cancellation?
        break;

    case DSA_ENQUEUE_JOB: {
        if (deployment_doc == NULL) {
            GGL_LOGE(
                "Job status is queued/in progress, but no deployment doc was "
                "given."
            );
            return GGL_ERR_INVALID;
        }
        (void) enqueue_job(deployment_doc->map, job_id->buf);
        break;
    }
    default:
        break;
    }
    return GGL_ERR_OK;
}

static GglError next_job_execution_changed_callback(
    void *ctx, uint32_t handle, GglObject data
) {
    (void) ctx;
    (void) handle;
    GglBumpAlloc json_allocator
        = ggl_bump_alloc_init(GGL_BUF(subscription_scratch));
    GglObject json = GGL_OBJ_NULL();
    GglError ret = deserialize_payload(&json_allocator.alloc, data, &json);
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }
    if (json.type != GGL_TYPE_MAP) {
        GGL_LOGE("JSON was not a map");
        return GGL_ERR_FAILURE;
    }

    GglObject *job_execution = NULL;
    ret = ggl_map_validate(
        json.map,
        GGL_MAP_SCHEMA(
            { GGL_STR("execution"), false, GGL_TYPE_MAP, &job_execution }
        )
    );
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }
    if (job_execution == NULL) {
        // TODO: job cancellation
        return GGL_ERR_OK;
    }
    ret = process_job_execution(job_execution->map);
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}

static GglError subscribe_to_next_job_topics(void) {
    GGL_MTX_SCOPE_GUARD(&topic_scratch_mutex);

    if (next_job_handle == 0) {
        GglBuffer job_topic = GGL_BUF(topic_scratch);
        GglError err = create_next_job_execution_changed_topic(
            thing_name_buf, &job_topic
        );
        if (err != GGL_ERR_OK) {
            return err;
        }
        return ggl_aws_iot_mqtt_subscribe(
            GGL_BUF_LIST(job_topic),
            QOS_AT_LEAST_ONCE,
            next_job_execution_changed_callback,
            NULL,
            NULL,
            &next_job_handle
        );
    }

    return GGL_ERR_OK;
}

// Make subscriptions and kick off IoT Jobs Workflow
void listen_for_jobs_deployments(void) {
    // TODO: reconnecting to MQTT should call this function
    // TODO: use backoff library for random retry interval
    int64_t retries = 1;
    while (get_thing_name() != GGL_ERR_OK) {
        int64_t sleep_for = 1 << MIN(2, retries);
        ggl_sleep(1 << sleep_for);
        ++retries;
    }

    // Following "Get the next job" workflow
    // https://docs.aws.amazon.com/iot/latest/developerguide/jobs-workflow-device-online.html

    next_job_handle = 0;

    retries = 1;
    while (subscribe_to_next_job_topics() != GGL_ERR_OK) {
        int64_t sleep_for = 1 << MIN(5, retries);
        ggl_sleep(sleep_for);
        ++retries;
    }

    retries = 1;
    while (describe_next_job() != GGL_ERR_OK) {
        int64_t sleep_for = 1 << MIN(5, retries);
        ggl_sleep(sleep_for);
        ++retries;
    }
}

GglError update_current_jobs_deployment(
    GglBuffer deployment_id, GglBuffer status
) {
    if (!ggl_buffer_eq(deployment_id, current_deployment_id.buf)) {
        return GGL_ERR_NOENTRY;
    }

    // TODO: mutual exclusion
    // Subscription thread gets a cancellation and then a new job,
    // overwriting current_job_id while deployment thread updates job state
    return update_job(current_job_id.buf, status, &current_job_version);
}
