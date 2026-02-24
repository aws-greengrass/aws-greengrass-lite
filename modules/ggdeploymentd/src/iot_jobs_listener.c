// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "iot_jobs_listener.h"
#include "bootstrap_manager.h"
#include "deployment_model.h"
#include "deployment_queue.h"
#include <gg/arena.h>
#include <gg/backoff.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/json_decode.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/utils.h>
#include <gg/vector.h>
#include <ggl/aws_iot_call.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdnoreturn.h>

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

static GgBuffer thing_name_buf;

static pthread_mutex_t current_job_id_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint8_t current_job_id_buf[64];
static GgByteVec current_job_id;
static uint8_t current_deployment_id_buf[64];
static GgByteVec current_deployment_id;

static pthread_mutex_t listener_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t listener_cond = PTHREAD_COND_INITIALIZER;
static bool needs_describe = false;

static void listen_for_jobs_deployments(void);

static GgError create_get_next_job_topic(
    GgBuffer thing_name, GgBuffer *job_topic
) {
    GgByteVec job_topic_vec = gg_byte_vec_init(*job_topic);
    GgError err = GG_ERR_OK;
    gg_byte_vec_chain_append(&err, &job_topic_vec, GG_STR(THINGS_TOPIC_PREFIX));
    gg_byte_vec_chain_append(&err, &job_topic_vec, thing_name);
    gg_byte_vec_chain_append(&err, &job_topic_vec, GG_STR(JOBS_TOPIC_PREFIX));
    gg_byte_vec_chain_append(&err, &job_topic_vec, GG_STR(NEXT_JOB_LITERAL));
    gg_byte_vec_chain_append(&err, &job_topic_vec, GG_STR(JOBS_GET_TOPIC));
    if (err == GG_ERR_OK) {
        *job_topic = job_topic_vec.buf;
    }
    return err;
}

static GgError create_update_job_topic(
    GgBuffer thing_name, GgBuffer job_id, GgBuffer *job_topic
) {
    GgByteVec job_topic_vec = gg_byte_vec_init(*job_topic);
    GgError err = GG_ERR_OK;
    gg_byte_vec_chain_append(&err, &job_topic_vec, GG_STR(THINGS_TOPIC_PREFIX));
    gg_byte_vec_chain_append(&err, &job_topic_vec, thing_name);
    gg_byte_vec_chain_append(&err, &job_topic_vec, GG_STR(JOBS_TOPIC_PREFIX));
    gg_byte_vec_chain_append(&err, &job_topic_vec, job_id);
    gg_byte_vec_chain_append(&err, &job_topic_vec, GG_STR(JOBS_UPDATE_TOPIC));
    if (err == GG_ERR_OK) {
        *job_topic = job_topic_vec.buf;
    }
    return err;
}

static GgError create_next_job_execution_changed_topic(
    GgBuffer thing_name, GgBuffer *job_topic
) {
    GgByteVec job_topic_vec = gg_byte_vec_init(*job_topic);
    GgError err = GG_ERR_OK;
    gg_byte_vec_chain_append(&err, &job_topic_vec, GG_STR(THINGS_TOPIC_PREFIX));
    gg_byte_vec_chain_append(&err, &job_topic_vec, thing_name);
    gg_byte_vec_chain_append(
        &err, &job_topic_vec, GG_STR(NEXT_JOB_EXECUTION_CHANGED_TOPIC)
    );
    if (err == GG_ERR_OK) {
        *job_topic = job_topic_vec.buf;
    }
    return err;
}

static GgError update_job(GgBuffer job_id, GgBuffer job_status);

static GgError process_job_execution(GgMap job_execution);

static GgError get_thing_name(void *ctx) {
    (void) ctx;
    GG_LOGD("Attempting to retrieve thing name");

    static uint8_t thing_name_mem[MAX_THING_NAME_LEN];
    GgArena alloc = gg_arena_init(GG_BUF(thing_name_mem));

    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("system"), GG_STR("thingName")),
        &alloc,
        &thing_name_buf
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to read thingName from config.");
        return ret;
    }

    return GG_ERR_OK;
}

// Decode MQTT payload as JSON into GgObject representation
static GgError deserialize_payload(
    GgArena *alloc, GgObject data, GgObject *json_object
) {
    GgBuffer topic = { 0 };
    GgBuffer payload = { 0 };

    GgError ret = ggl_aws_iot_mqtt_subscribe_parse_resp(data, &topic, &payload);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GG_LOGI(
        "Got message from IoT Core; topic: %.*s, payload: %.*s.",
        (int) topic.len,
        topic.data,
        (int) payload.len,
        payload.data
    );

    ret = gg_json_decode_destructive(payload, alloc, json_object);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to parse job doc JSON.");
        return ret;
    }
    return GG_ERR_OK;
}

static GgError update_job(GgBuffer job_id, GgBuffer job_status) {
    GgBuffer topic = GG_BUF((uint8_t[256]) { 0 });
    GgError ret = create_update_job_topic(thing_name_buf, job_id, &topic);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // expectedVersion omitted to avoid VersionMismatch errors on MQTT
    // reconnect races; only one ggdeploymentd updates a given job.
    GgObject payload_object = gg_obj_map(GG_MAP(
        gg_kv(GG_STR("status"), gg_obj_buf(job_status)),
        gg_kv(GG_STR("clientToken"), gg_obj_buf(GG_STR("jobs-nucleus-lite")))
    ));

    static uint8_t response_scratch[512];
    GgArena call_alloc = gg_arena_init(GG_BUF(response_scratch));
    GgObject result = { 0 };
    ret = ggl_aws_iot_call(
        GG_STR("aws_iot_mqtt"),
        topic,
        payload_object,
        false,
        &call_alloc,
        &result
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to publish on update job topic.");
        return GG_ERR_FAILURE;
    }

    // save jobs ID to config in case of bootstrap
    ret = save_iot_jobs_id(job_id);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to save job ID to config.");
        return ret;
    }

    return GG_ERR_OK;
}

static GgError describe_next_job(void *ctx) {
    (void) ctx;
    GG_LOGD("Requesting next job information.");
    static uint8_t topic_scratch[512];
    GgBuffer topic = GG_BUF(topic_scratch);
    GgError ret = create_get_next_job_topic(thing_name_buf, &topic);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // https://docs.aws.amazon.com/iot/latest/developerguide/jobs-mqtt-api.html
    GgObject payload_object = gg_obj_map(GG_MAP(
        gg_kv(GG_STR("jobId"), gg_obj_buf(GG_STR(NEXT_JOB_LITERAL))),
        gg_kv(GG_STR("thingName"), gg_obj_buf(thing_name_buf)),
        gg_kv(GG_STR("includeJobDocument"), gg_obj_bool(true)),
        gg_kv(GG_STR("clientToken"), gg_obj_buf(GG_STR("jobs-nucleus-lite")))
    ));

    static uint8_t response_scratch[4096];
    GgArena call_alloc = gg_arena_init(GG_BUF(response_scratch));
    GgObject job_description;
    ret = ggl_aws_iot_call(
        GG_STR("aws_iot_mqtt"),
        topic,
        payload_object,
        false,
        &call_alloc,
        &job_description
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to publish on describe job topic");
        return ret;
    }

    if (gg_obj_type(job_description) != GG_TYPE_MAP) {
        GG_LOGE("Describe payload not of type Map");
        return GG_ERR_FAILURE;
    }

    GgObject *execution = NULL;
    ret = gg_map_validate(
        gg_obj_into_map(job_description),
        GG_MAP_SCHEMA(
            { GG_STR("execution"), GG_OPTIONAL, GG_TYPE_MAP, &execution }
        )
    );
    if (ret != GG_ERR_OK) {
        return GG_ERR_FAILURE;
    }
    if (execution == NULL) {
        GG_LOGD("No deployment to process.");
        return GG_ERR_OK;
    }
    GG_LOGD("Processing execution.");
    return process_job_execution(gg_obj_into_map(*execution));
}

static GgError enqueue_job(GgMap deployment_doc, GgBuffer job_id) {
    GgError ret;
    {
        GG_MTX_SCOPE_GUARD(&current_job_id_mutex);
        if (gg_buffer_eq(current_job_id.buf, job_id)) {
            GG_LOGI("Duplicate job document received. Skipping.");
            return GG_ERR_OK;
        }

        current_job_id = GG_BYTE_VEC(current_job_id_buf);
        ret = gg_byte_vec_append(&current_job_id, job_id);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Job ID too long.");
            return ret;
        }

        current_deployment_id = GG_BYTE_VEC(current_deployment_id_buf);

        // TODO: backoff algorithm
        int64_t retries = 1;
        while (
            (ret = ggl_deployment_enqueue(
                 deployment_doc, &current_deployment_id, THING_GROUP_DEPLOYMENT
             ))
            == GG_ERR_BUSY
        ) {
            int64_t sleep_for = 1 << MIN(7, retries);
            (void) gg_sleep(sleep_for);
            ++retries;
        }
    }

    if (ret != GG_ERR_OK) {
        (void) update_job(job_id, GG_STR("FAILURE"));
    }

    return ret;
}

static GgError process_job_execution(GgMap job_execution) {
    GgObject *job_id = NULL;
    GgObject *status = NULL;
    GgObject *deployment_doc = NULL;
    GgError err = gg_map_validate(
        job_execution,
        GG_MAP_SCHEMA(
            { GG_STR("jobId"), GG_OPTIONAL, GG_TYPE_BUF, &job_id },
            { GG_STR("status"), GG_OPTIONAL, GG_TYPE_BUF, &status },
            { GG_STR("jobDocument"), GG_OPTIONAL, GG_TYPE_MAP, &deployment_doc }
        )
    );
    if (err != GG_ERR_OK) {
        GG_LOGE("Failed to validate job execution response.");
        return GG_ERR_FAILURE;
    }
    if ((status == NULL) || (job_id == NULL)) {
        return GG_ERR_OK;
    }
    DeploymentStatusAction action;
    {
        GgMap status_action_map = GG_MAP(
            gg_kv(GG_STR("QUEUED"), gg_obj_i64(DSA_ENQUEUE_JOB)),
            gg_kv(GG_STR("IN_PROGRESS"), gg_obj_i64(DSA_ENQUEUE_JOB)),
            gg_kv(GG_STR("SUCCEEDED"), gg_obj_i64(DSA_DO_NOTHING)),
            gg_kv(GG_STR("FAILED"), gg_obj_i64(DSA_DO_NOTHING)),
            gg_kv(GG_STR("TIMED_OUT"), gg_obj_i64(DSA_CANCEL_JOB)),
            gg_kv(GG_STR("REJECTED"), gg_obj_i64(DSA_DO_NOTHING)),
            gg_kv(GG_STR("REMOVED"), gg_obj_i64(DSA_CANCEL_JOB)),
            gg_kv(GG_STR("CANCELED"), gg_obj_i64(DSA_CANCEL_JOB)),
        );
        GgObject *integer = NULL;
        if (!gg_map_get(
                status_action_map, gg_obj_into_buf(*status), &integer
            )) {
            GG_LOGE("Job status not a valid value");
            return GG_ERR_INVALID;
        }
        action = (DeploymentStatusAction) gg_obj_into_i64(*integer);
    }
    switch (action) {
    case DSA_CANCEL_JOB:
        // TODO: cancelation?
        break;

    case DSA_ENQUEUE_JOB: {
        if (deployment_doc == NULL) {
            GG_LOGE(
                "Job status is queued/in progress, but no deployment doc was given."
            );
            return GG_ERR_INVALID;
        }
        (void) enqueue_job(
            gg_obj_into_map(*deployment_doc), gg_obj_into_buf(*job_id)
        );
        break;
    }
    default:
        break;
    }
    return GG_ERR_OK;
}

static GgError next_job_execution_changed_callback(
    void *ctx, uint32_t handle, GgObject data
) {
    (void) ctx;
    (void) handle;
    GG_LOGD("Received next job execution changed response.");
    static uint8_t subscription_scratch[4096];
    GgArena json_allocator = gg_arena_init(GG_BUF(subscription_scratch));
    GgObject json;
    GgError ret = deserialize_payload(&json_allocator, data, &json);
    if (ret != GG_ERR_OK) {
        return GG_ERR_FAILURE;
    }
    if (gg_obj_type(json) != GG_TYPE_MAP) {
        GG_LOGE("JSON was not a map");
        return GG_ERR_FAILURE;
    }

    GgObject *job_execution = NULL;
    ret = gg_map_validate(
        gg_obj_into_map(json),
        GG_MAP_SCHEMA(
            { GG_STR("execution"), GG_OPTIONAL, GG_TYPE_MAP, &job_execution }
        )
    );
    if (ret != GG_ERR_OK) {
        return GG_ERR_FAILURE;
    }
    if (job_execution == NULL) {
        // TODO: job cancelation
        return GG_ERR_OK;
    }
    ret = process_job_execution(gg_obj_into_map(*job_execution));
    if (ret != GG_ERR_OK) {
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}

noreturn void *job_listener_thread(void *ctx) {
    (void) ctx;
    (void) gg_backoff(1, 1000, 0, get_thing_name, NULL);
    listen_for_jobs_deployments();

    // coverity[infinite_loop]
    while (true) {
        {
            GG_MTX_SCOPE_GUARD(&listener_mutex);
            while (!needs_describe) {
                pthread_cond_wait(&listener_cond, &listener_mutex);
            }
            needs_describe = false;
        }
        (void) gg_backoff(10, 10000, 0, describe_next_job, NULL);
    }
}

static void resubscribe_on_iotcored_close(void *ctx, uint32_t handle) {
    (void) ctx;
    (void) handle;
    GG_LOGD("Subscriptions closed. Subscribing again.");
    listen_for_jobs_deployments();
}

static GgError subscribe_to_next_job_topics(void *ctx) {
    (void) ctx;
    static uint8_t topic_scratch[256];
    GgBuffer job_topic = GG_BUF(topic_scratch);
    GgError err
        = create_next_job_execution_changed_topic(thing_name_buf, &job_topic);
    if (err != GG_ERR_OK) {
        return err;
    }
    return ggl_aws_iot_mqtt_subscribe(
        GG_STR("aws_iot_mqtt"),
        GG_BUF_LIST(job_topic),
        QOS_AT_LEAST_ONCE,
        false,
        next_job_execution_changed_callback,
        resubscribe_on_iotcored_close,
        NULL,
        NULL
    );
}

static GgError iot_jobs_on_reconnect(
    void *ctx, uint32_t handle, GgObject data
) {
    (void) ctx;
    (void) handle;
    if (gg_obj_into_bool(data)) {
        GG_LOGD("Reconnected to MQTT; requesting new job query publish.");
        GG_MTX_SCOPE_GUARD(&listener_mutex);
        needs_describe = true;
        pthread_cond_signal(&listener_cond);
    }
    return GG_ERR_OK;
}

static GgError subscribe_to_connection_status(void *ctx) {
    (void) ctx;
    return ggl_subscribe(
        GG_STR("aws_iot_mqtt"),
        GG_STR("connection_status"),
        GG_MAP(),
        iot_jobs_on_reconnect,
        NULL,
        NULL,
        NULL,
        NULL
    );
}

// Make subscriptions and kick off IoT Jobs Workflow
static void listen_for_jobs_deployments(void) {
    // Following "Get the next job" workflow
    // https://docs.aws.amazon.com/iot/latest/developerguide/jobs-workflow-device-online.html
    GG_LOGD("Subscribing to IoT Jobs topics.");
    (void) gg_backoff(10, 10000, 0, subscribe_to_next_job_topics, NULL);
    (void) gg_backoff(10, 10000, 0, subscribe_to_connection_status, NULL);
}

GgError update_current_jobs_deployment(
    GgBuffer deployment_id, GgBuffer status
) {
    GgBuffer job_id = GG_BUF((uint8_t[64]) { 0 });
    {
        GG_MTX_SCOPE_GUARD(&current_job_id_mutex);
        if (!gg_buffer_eq(deployment_id, current_deployment_id.buf)) {
            return GG_ERR_NOENTRY;
        }
        memcpy(
            job_id.data, current_job_id.buf.data, current_deployment_id.buf.len
        );
        job_id.len = current_deployment_id.buf.len;
    }

    return update_job(job_id, status);
}

GgError set_jobs_deployment_for_bootstrap(
    GgBuffer job_id, GgBuffer deployment_id
) {
    GG_MTX_SCOPE_GUARD(&current_job_id_mutex);
    if (!gg_buffer_eq(job_id, current_job_id.buf)) {
        if (current_job_id.buf.len != 0) {
            GG_LOGI("Bootstrap deployment was canceled by cloud.");
            return GG_ERR_NOENTRY;
        }
        current_job_id = GG_BYTE_VEC(current_job_id_buf);
        GgError ret = gg_byte_vec_append(&current_job_id, job_id);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Job ID too long.");
            return ret;
        }
        current_deployment_id = GG_BYTE_VEC(current_deployment_id_buf);
        ret = gg_byte_vec_append(&current_deployment_id, deployment_id);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Deployment ID too long.");
            return ret;
        }
    }
    return GG_ERR_OK;
}
