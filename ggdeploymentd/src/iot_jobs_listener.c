// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "iot_jobs_listener.h"
#include "deployment_queue.h"
#include <sys/types.h>
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/json_decode.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/utils.h>
#include <ggl/vector.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

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
#define UPDATE_JOB_TOPIC \
    "$aws/things/{}/jobs/{}/namespace-aws-gg-deployment/update"
#define JOB_UPDATE_ACCEPTED_TOPIC \
    "$aws/things/{}/jobs/+/namespace-aws-gg-deployment/update/accepted"
#define JOB_UPDATE_REJECTED_TOPIC \
    "$aws/things/{}/jobs/+/namespace-aws-gg-deployment/update/rejected"
#define DESCRIBE_JOB_TOPIC \
    "$aws/things/{}/jobs/{}/namespace-aws-gg-deployment/get"
#define JOB_DESCRIBE_ACCEPTED_TOPIC \
    "$aws/things/{}/jobs/{}/namespace-aws-gg-deployment/get/accepted"
#define JOB_DESCRIBE_REJECTED_TOPIC \
    "$aws/things/{}/jobs/{}/namespace-aws-gg-deployment/get/rejected"
#define NEXT_JOB_EXECUTION_CHANGED_TOPIC \
    "$aws/things/{}/jobs/notify-next-namespace-aws-gg-deployment"

#define NEXT_JOB_LITERAL "$next"

// TODO: remove when adding backoff algorithm
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

static uint8_t thing_name_buf[128];
static GglByteVec thing_name;

static uint8_t current_job_id_buf[64];
static GglByteVec current_job_id;
static uint8_t current_deployment_id_buf[64];
static GglByteVec current_deployment_id;
static int64_t current_job_version;

pthread_mutex_t topic_scratch_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint8_t topic_scratch[256];

static uint8_t subscription_thread_scratch[4096];

// aws_iot_mqtt subscription handles
static uint32_t next_job_handle;
static uint32_t get_accepted_handle;
static uint32_t get_rejected_handle;
static uint32_t update_accepted_handle;
static uint32_t update_rejected_handle;

static GglError update_job(
    GglBuffer job_id, GglBuffer job_status, int64_t *version
);
static GglError process_job_execution(GglMap job_execution);

// replace every occurrence of '{}' in the format string with each
// successive buffer in values. Formatted string appended into output.
static GglError ggl_byte_vec_format(
    GglByteVec *output, GglBuffer format, GglList values
) {
    size_t value = 0;
    while (format.len > 0) {
        size_t idx;
        for (idx = 0; idx < format.len - 1; ++idx) {
            if ((format.data[idx] == '{') && (format.data[idx + 1] == '}')) {
                break;
            }
        }

        if (idx >= format.len - 1) {
            break;
        }

        if (value >= values.len) {
            GGL_LOGE("format", "not enough format values");
            return GGL_ERR_RANGE;
        }

        if (values.items[value].type != GGL_TYPE_BUF) {
            GGL_LOGE("format", "format value type not supported");
            return GGL_ERR_INVALID;
        }

        GglBuffer prefix = ggl_buffer_substr(format, 0, idx);
        GglError ret = ggl_byte_vec_append(output, prefix);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        ret = ggl_byte_vec_append(output, values.items[value].buf);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        format = ggl_buffer_substr(format, idx + 2, format.len);
        ++value;
    }

    if (value != values.len) {
        GGL_LOGW("format", "too many format values");
    }

    if (format.len > 0) {
        return ggl_byte_vec_append(output, format);
    }
    return GGL_ERR_OK;
}

// Retrieve thingName from config
static GglError get_thing_name(void) {
    GglMap params = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("thingName")) }
    );

    uint8_t response_buffer[128 + 2 * sizeof(GglObject)] = { 0 };
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(response_buffer));

    GglObject resp;
    GglError ret = ggl_call(
        GGL_STR("gg_config"),
        GGL_STR("read"),
        params,
        NULL,
        &balloc.alloc,
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("jobs-listener", "Failed to get thing name from config.");
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE("jobs-listener", "Configuration thing name is not a string.");
        return GGL_ERR_INVALID;
    }

    thing_name = GGL_BYTE_VEC(thing_name_buf);
    return ggl_byte_vec_append(&thing_name, resp.buf);
}

// Decode MQTT payload as JSON into GglObject representation
static GglError deserialize_payload(
    GglAlloc *alloc, GglObject data, GglObject *json_object
) {
    if (data.type != GGL_TYPE_MAP) {
        GGL_LOGE("jobs-listener", "Subscription response is not a map.");
        return GGL_ERR_FAILURE;
    }

    GglBuffer topic = GGL_STR("");
    GglBuffer payload = GGL_STR("");

    GglObject *val;
    if (ggl_map_get(data.map, GGL_STR("topic"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE(
                "jobs-listener", "Subscription response topic not a buffer."
            );
            return GGL_ERR_FAILURE;
        }
        topic = val->buf;
    } else {
        GGL_LOGE("jobs-listener", "Subscription response is missing topic.");
        return GGL_ERR_FAILURE;
    }
    if (ggl_map_get(data.map, GGL_STR("payload"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE(
                "jobs-listener", "Subscription response payload not a buffer."
            );
            return GGL_ERR_FAILURE;
        }
        payload = val->buf;
    } else {
        GGL_LOGE("jobs-listener", "Subscription response is missing payload.");
        return GGL_ERR_FAILURE;
    }

    GGL_LOGI(
        "jobs-listener",
        "Got message from IoT Core; topic: %.*s, payload: %.*s.",
        (int) topic.len,
        topic.data,
        (int) payload.len,
        payload.data
    );

    GglError ret = ggl_json_decode_destructive(payload, alloc, json_object);

    if (ret != GGL_ERR_OK) {
        GGL_LOGE("jobs-listener", "failed to parse job doc JSON");
        return ret;
    }
    return GGL_ERR_OK;
}

static GglError update_job(
    GglBuffer job_id, GglBuffer job_status, int64_t *version
) {
    GglBuffer aws_iot_mqtt = GGL_STR("aws_iot_mqtt");

    pthread_mutex_lock(&topic_scratch_mutex);
    GGL_DEFER(pthread_mutex_unlock, topic_scratch_mutex);
    GglByteVec topic = GGL_BYTE_VEC(topic_scratch);

    topic.buf.len = 0;
    GglError ret = ggl_byte_vec_format(
        &topic,
        GGL_STR(UPDATE_JOB_TOPIC),
        GGL_LIST(GGL_OBJ(thing_name.buf), GGL_OBJ(job_id))
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    uint8_t version_buf[16] = { 0 };
    int len = snprintf(
        (char *) version_buf, sizeof(version_buf), "%" PRIi64, *version
    );
    if (len <= 0) {
        GGL_LOGE("jobs-listener", "Version too big");
        return GGL_ERR_RANGE;
    }

    // https://docs.aws.amazon.com/iot/latest/developerguide/jobs-mqtt-api.html
    GglObject payload_object = GGL_OBJ_MAP(
        { GGL_STR("status"), GGL_OBJ(job_status) },
        { GGL_STR("expectedVersion"),
          GGL_OBJ((GglBuffer) { .data = version_buf, .len = (size_t) len }) },
    );

    // {"expectedVersion":"123456789012345","status":"IN_PROGRESS"}
    uint8_t payload_bytes[64];
    GglBuffer payload = GGL_BUF(payload_bytes);
    ret = ggl_json_encode(payload_object, &payload);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("jobs-listener", "Payload buffer too small");
        return ret;
    }

    GglMap publish_args = GGL_MAP(
        { GGL_STR("topic"), GGL_OBJ(topic.buf) },
        { GGL_STR("payload"), GGL_OBJ(payload) },
        { GGL_STR("qos"), GGL_OBJ_I64(QOS_AT_LEAST_ONCE) }
    );
    GGL_LOGW("jobs-listener", "%.*s", (int) payload.len, (char *) payload.data);

    ret = ggl_notify(aws_iot_mqtt, GGL_STR("publish"), publish_args);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("jobs-listener", "Failed to publish on update job topic");
        return ret;
    }

    GGL_LOGI(
        "jobs-listener", "Update packet seems to be sent. Incrementing version"
    );
    ++(*version);
    return GGL_ERR_OK;
}

static GglError describe_accepted_callback(
    void *ctx, uint32_t handle, GglObject data

) {
    (void) ctx;
    (void) handle;
    (void) data;
    GGL_LOGI("jobs-listener", "describe accepted");
    GglBumpAlloc json_alloc
        = ggl_bump_alloc_init(GGL_BUF(subscription_thread_scratch));
    GglObject job_description = GGL_OBJ_NULL();
    GglError ret
        = deserialize_payload(&json_alloc.alloc, data, &job_description);

    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }

    if (job_description.type != GGL_TYPE_MAP) {
        GGL_LOGE("jobs-listener", "Describe payload not of type Map");
        return GGL_ERR_FAILURE;
    }

    GglObject *execution = NULL;
    if (!ggl_map_get(job_description.map, GGL_STR("execution"), &execution)) {
        return GGL_ERR_OK;
    }

    if (execution->type != GGL_TYPE_MAP) {
        GGL_LOGE("jobs-listener", "Job execution not a map");
        return GGL_ERR_FAILURE;
    }

    return process_job_execution(execution->map);
}

static GglError describe_rejected_callback(
    void *ctx, uint32_t handle, GglObject data
) {
    (void) ctx;
    (void) handle;
    (void) data;
    GGL_LOGW("jobs-listener", "Describe request rejected");
    // TODO: retry logic?
    return GGL_ERR_OK;
}

static GglError describe_next_job(void) {
    GglBuffer aws_iot_mqtt = GGL_STR("aws_iot_mqtt");

    pthread_mutex_lock(&topic_scratch_mutex);
    GGL_DEFER(pthread_mutex_unlock, topic_scratch_mutex);
    GglByteVec topic = GGL_BYTE_VEC(topic_scratch);
    topic.buf.len = 0;
    GglError ret = ggl_byte_vec_format(
        &topic,
        GGL_STR(DESCRIBE_JOB_TOPIC),
        GGL_LIST(GGL_OBJ(thing_name.buf), GGL_OBJ_STR(NEXT_JOB_LITERAL))
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // https://docs.aws.amazon.com/iot/latest/developerguide/jobs-mqtt-api.html
    GglObject payload_object = GGL_OBJ_MAP(
        { GGL_STR("jobId"), GGL_OBJ_STR(NEXT_JOB_LITERAL) },
        { GGL_STR("thingName"), GGL_OBJ(thing_name.buf) },
        { GGL_STR("includeJobDocument"), GGL_OBJ_BOOL(true) }
    );

    uint8_t payload_bytes[196];
    GglBuffer payload = GGL_BUF(payload_bytes);
    ret = ggl_json_encode(payload_object, &payload);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("jobs-listener", "Payload buffer too small");
        return ret;
    }

    GglMap publish_args = GGL_MAP(
        { GGL_STR("topic"), GGL_OBJ(topic.buf) },
        { GGL_STR("payload"), GGL_OBJ(payload) },
        { GGL_STR("qos"), GGL_OBJ_I64(QOS_AT_LEAST_ONCE) }
    );

    ret = ggl_notify(aws_iot_mqtt, GGL_STR("publish"), publish_args);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("jobs-listener", "Failed to publish on describe job topic");
        return ret;
    }
    return GGL_ERR_OK;
}

static GglError enqueue_job(GglMap deployment_doc, GglBuffer job_id) {
    // TODO: check if current job is canceled before clobbering
    current_job_version = 1;
    current_job_id = GGL_BYTE_VEC(current_job_id_buf);
    ggl_byte_vec_append(&current_job_id, job_id);
    current_deployment_id = GGL_BYTE_VEC(current_deployment_id_buf);

    // TODO: backoff algorithm
    GglError ret = GGL_ERR_OK;
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
    if (!ggl_map_get(job_execution, GGL_STR("jobId"), &job_id)) {
        GGL_LOGW("jobs-listener", "Job does not have jobID");
        return GGL_ERR_OK;
    }
    if (job_id->type != GGL_TYPE_BUF) {
        GGL_LOGE("jobs-listener", "Job ID not of type String");
        return GGL_ERR_INVALID;
    }

    GglObject *status = NULL;
    if (!ggl_map_get(job_execution, GGL_STR("status"), &status)) {
        return GGL_ERR_OK;
    }

    if (status->type != GGL_TYPE_BUF) {
        GGL_LOGE("jobs-listener", "Job status not of type String");
        return GGL_ERR_INVALID;
    }

    DeploymentStatusAction action;
    {
        GglMap status_action_map = GGL_MAP(
            { GGL_STR("QUEUED"), GGL_OBJ_I64(DSA_ENQUEUE_JOB) },
            { GGL_STR("IN_PROGRESS"), GGL_OBJ_BOOL(DSA_DO_NOTHING) },
            { GGL_STR("SUCCEEDED"), GGL_OBJ_BOOL(DSA_DO_NOTHING) },
            { GGL_STR("FAILED"), GGL_OBJ_BOOL(DSA_DO_NOTHING) },
            { GGL_STR("TIMED_OUT"), GGL_OBJ_BOOL(DSA_CANCEL_JOB) },
            { GGL_STR("REJECTED"), GGL_OBJ_BOOL(DSA_DO_NOTHING) },
            { GGL_STR("REMOVED"), GGL_OBJ_BOOL(DSA_CANCEL_JOB) },
            { GGL_STR("CANCELED"), GGL_OBJ_BOOL(DSA_CANCEL_JOB) },
        );
        GglObject *integer = NULL;
        if (!ggl_map_get(status_action_map, status->buf, &integer)) {
            GGL_LOGE("jobs-listener", "Job status not a valid value");
            return GGL_ERR_INVALID;
        }
        action = (DeploymentStatusAction) integer->i64;
    }
    switch (action) {
    case DSA_CANCEL_JOB:
        // TODO: cancellation?
        break;

    case DSA_ENQUEUE_JOB: {
        GglObject *deployment_doc = NULL;
        if (!ggl_map_get(
                job_execution, GGL_STR("jobDocument"), &deployment_doc
            )) {
            GGL_LOGE("jobs-listener", "Job document not found.");
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
        = ggl_bump_alloc_init(GGL_BUF(subscription_thread_scratch));
    GglObject json = GGL_OBJ_NULL();
    GglError ret = deserialize_payload(&json_allocator.alloc, data, &json);
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }

    if (json.type != GGL_TYPE_MAP) {
        GGL_LOGE("jobs-listener", "JSON was not a map");
        return GGL_ERR_FAILURE;
    }

    GglObject *job_execution = NULL;
    if (!ggl_map_get(json.map, GGL_STR("execution"), &job_execution)) {
        // TODO: handle cancellation
        return GGL_ERR_OK;
    }

    if (job_execution->type != GGL_TYPE_MAP) {
        GGL_LOGE("jobs-listener", "Job execution state not of type Object");
        return GGL_ERR_FAILURE;
    }
    ret = process_job_execution(job_execution->map);
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}

static GglError job_update_accepted_callback(
    void *ctx, uint32_t handle, GglObject data
) {
    (void) ctx;
    (void) handle;
    (void) data;
    GGL_LOGI("jobs-listener", "Job update accepted");
    uint8_t rejected_payload[512];
    GglBumpAlloc bump_allocator
        = ggl_bump_alloc_init(GGL_BUF(rejected_payload));
    GglObject json = GGL_OBJ_NULL();
    deserialize_payload(&bump_allocator.alloc, data, &json);
    return GGL_ERR_OK;
}

static GglError job_update_rejected_callback(
    void *ctx, uint32_t handle, GglObject data
) {
    (void) ctx;
    (void) handle;
    (void) data;

    GGL_LOGW("jobs-listener", "Job update rejected");
    uint8_t rejected_payload[512];
    GglBumpAlloc bump_allocator
        = ggl_bump_alloc_init(GGL_BUF(rejected_payload));
    GglObject json = GGL_OBJ_NULL();
    GglError ret = deserialize_payload(&bump_allocator.alloc, data, &json);
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }
    // TODO: retry logic?
    return GGL_ERR_OK;
}

static GglError subscribe_to_format_topic(
    GglByteVec topic,
    GglBuffer format,
    GglList values,
    GglSubscribeCallback on_response,
    uint32_t *handle
) {
    GglBuffer aws_iot_mqtt = GGL_STR("aws_iot_mqtt");

    topic.buf.len = 0;
    GglError ret = ggl_byte_vec_format(&topic, format, values);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return ggl_subscribe(
        aws_iot_mqtt,
        GGL_STR("subscribe"),
        GGL_MAP(
            { GGL_STR("topic_filter"), GGL_OBJ(topic.buf) },
            { GGL_STR("qos"), GGL_OBJ_I64(QOS_AT_LEAST_ONCE) }
        ),
        on_response,
        NULL,
        NULL,
        NULL,
        handle
    );
}

static GglError subscribe_to_next_job_topics(void) {
    pthread_mutex_lock(&topic_scratch_mutex);
    GGL_DEFER(pthread_mutex_unlock, topic_scratch_mutex);
    GglByteVec topic = GGL_BYTE_VEC(topic_scratch);

    // TODO: batch these
    if (next_job_handle == 0) {
        GglError ret = subscribe_to_format_topic(
            topic,
            GGL_STR(NEXT_JOB_EXECUTION_CHANGED_TOPIC),
            GGL_LIST(GGL_OBJ(thing_name.buf)),
            next_job_execution_changed_callback,
            &next_job_handle
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    if (get_accepted_handle == 0) {
        GglError ret = subscribe_to_format_topic(
            topic,
            GGL_STR(JOB_DESCRIBE_ACCEPTED_TOPIC),
            GGL_LIST(GGL_OBJ(thing_name.buf), GGL_OBJ_STR(NEXT_JOB_LITERAL)),
            describe_accepted_callback,
            &get_accepted_handle
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    if (get_rejected_handle == 0) {
        GglError ret = subscribe_to_format_topic(
            topic,
            GGL_STR(JOB_DESCRIBE_REJECTED_TOPIC),
            GGL_LIST(GGL_OBJ(thing_name.buf), GGL_OBJ_STR(NEXT_JOB_LITERAL)),
            describe_rejected_callback,
            &get_rejected_handle
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    if (update_accepted_handle == 0) {
        GglError ret = subscribe_to_format_topic(
            topic,
            GGL_STR(JOB_UPDATE_ACCEPTED_TOPIC),
            GGL_LIST(GGL_OBJ(thing_name.buf)),
            job_update_accepted_callback,
            &update_accepted_handle
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    if (update_rejected_handle == 0) {
        GglError ret = subscribe_to_format_topic(
            topic,
            GGL_STR(JOB_UPDATE_REJECTED_TOPIC),
            GGL_LIST(GGL_OBJ(thing_name.buf)),
            job_update_rejected_callback,
            &update_rejected_handle
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
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
    get_accepted_handle = 0;
    get_rejected_handle = 0;
    update_accepted_handle = 0;
    update_rejected_handle = 0;

    retries = 1;
    GglError ret = GGL_ERR_OK;
    while ((ret = subscribe_to_next_job_topics()) == GGL_ERR_RETRY) {
        int64_t sleep_for = 1 << MIN(5, retries);
        ggl_sleep(sleep_for);
        ++retries;
    }
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("jobs-listener", "Unable to retry subscriptions");
    }

    retries = 1;
    while ((ret = describe_next_job()) == GGL_ERR_RETRY) {
        int64_t sleep_for = 1 << MIN(5, retries);
        ggl_sleep(sleep_for);
        ++retries;
    }
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("jobs-listener", "Unable to retry first publish");
    }
}

GglError update_current_jobs_deployment(
    GglBuffer deployment_id, GglBuffer status
) {
    if (!ggl_buffer_eq(deployment_id, current_deployment_id.buf)) {
        return GGL_ERR_NOENTRY;
    }

    if (ggl_buffer_eq(status, GGL_STR("SUCCEEDED"))) {
        // TODO: hack
        // Update messages can arrive at IoT Core out-of-order
        // Which causes them to be rejected for version mismatch
        // SUCCEEDED with Version 2 will be rejected if no
        // update with Version 1 is first accepted
        ggl_sleep(10);
    }

    // TODO: mutual exclusion
    // Subscription thread gets a cancellation and then a new job,
    // overwriting current_job_id while deployment thread updates job state
    return update_job(current_job_id.buf, status, &current_job_version);
}
