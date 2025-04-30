// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "iot_jobs_listener.h"
#include "bootstrap_manager.h"
#include "deployment_model.h"
#include "deployment_queue.h"
#include <ggl/arena.h>
#include <ggl/aws_iot_call.h>
#include <ggl/backoff.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/flags.h>
#include <ggl/json_decode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/utils.h>
#include <ggl/vector.h>
#include <inttypes.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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

static GglBuffer thing_name_buf;

static pthread_mutex_t current_job_id_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint8_t current_job_id_buf[64];
static GglByteVec current_job_id;
static uint8_t current_deployment_id_buf[64];
static GglByteVec current_deployment_id;
static _Atomic int32_t current_job_version;

static pthread_mutex_t listener_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t listener_cond = PTHREAD_COND_INITIALIZER;
static bool needs_describe = false;

static void listen_for_jobs_deployments(void);

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
    GglBuffer job_id, GglBuffer job_status, _Atomic int32_t *version
);

static GglError process_job_execution(GglMap job_execution);

static GglError get_thing_name(void *ctx) {
    (void) ctx;
    GGL_LOGD("Attempting to retrieve thing name");

    static uint8_t thing_name_mem[MAX_THING_NAME_LEN];
    GglArena alloc = ggl_arena_init(GGL_BUF(thing_name_mem));

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("thingName")),
        &alloc,
        &thing_name_buf
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to read thingName from config.");
        return ret;
    }

    return GGL_ERR_OK;
}

// Decode MQTT payload as JSON into GglObject representation
static GglError deserialize_payload(
    GglArena *alloc, GglObject data, GglObject *json_object
) {
    GglBuffer topic = { 0 };
    GglBuffer payload = { 0 };

    GglError ret
        = ggl_aws_iot_mqtt_subscribe_parse_resp(data, &topic, &payload);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_LOGI(
        "Got message from IoT Core; topic: %.*s, payload: %.*s.",
        (int) topic.len,
        topic.data,
        (int) payload.len,
        payload.data
    );

    ret = ggl_json_decode_destructive(payload, alloc, json_object);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to parse job doc JSON.");
        return ret;
    }
    return GGL_ERR_OK;
}

static GglError update_job(
    GglBuffer job_id, GglBuffer job_status, _Atomic int32_t *version
) {
    GglBuffer topic = GGL_BUF((uint8_t[256]) { 0 });
    GglError ret = create_update_job_topic(thing_name_buf, job_id, &topic);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    int64_t local_version = atomic_load_explicit(version, memory_order_acquire);
    while (true) {
        uint8_t version_buf[16] = { 0 };
        int len = snprintf(
            (char *) version_buf, sizeof(version_buf), "%" PRIi64, local_version
        );
        if (len <= 0) {
            GGL_LOGE("Version too big");
            return GGL_ERR_RANGE;
        }
        // https://docs.aws.amazon.com/iot/latest/developerguide/jobs-mqtt-api.html
        GglObject payload_object = ggl_obj_map(GGL_MAP(
            { GGL_STR("status"), ggl_obj_buf(job_status) },
            { GGL_STR("expectedVersion"),
              ggl_obj_buf((GglBuffer) { .data = version_buf,
                                        .len = (size_t) len }) },
            { GGL_STR("clientToken"),
              ggl_obj_buf(GGL_STR("jobs-nucleus-lite")) }
        ));

        static uint8_t response_scratch[512];
        GglArena call_alloc = ggl_arena_init(GGL_BUF(response_scratch));
        GglObject result = { 0 };
        ret = ggl_aws_iot_call(topic, payload_object, &call_alloc, &result);
        if (ret == GGL_ERR_OK) {
            local_version
                // coverity[incompatible_param]
                = atomic_fetch_add_explicit(version, 1U, memory_order_acq_rel)
                + 1;
            break;
        }
        if (ret != GGL_ERR_REMOTE) {
            GGL_LOGE("Failed to publish on update job topic.");
            return GGL_ERR_FAILURE;
        }
        if (ggl_obj_type(result) != GGL_TYPE_MAP) {
            GGL_LOGD("Unknown job update rejected response received.");
            return GGL_ERR_PARSE;
        }
        GglObject *execution_state = NULL;
        if (!ggl_map_get(
                ggl_obj_into_map(result),
                GGL_STR("executionState"),
                &execution_state
            )) {
            GGL_LOGW("Unknown job update rejected response received.");
            return GGL_ERR_PARSE;
        }

        GglObject *remote_status = NULL;
        GglObject *remote_version = NULL;
        ret = ggl_map_validate(
            ggl_obj_into_map(*execution_state),
            GGL_MAP_SCHEMA(
                { GGL_STR("status"),
                  GGL_REQUIRED,
                  GGL_TYPE_BUF,
                  &remote_status },
                { GGL_STR("versionNumber"),
                  GGL_REQUIRED,
                  GGL_TYPE_I64,
                  &remote_version }
            )
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        if (ggl_buffer_eq(job_status, GGL_STR("CANCELED"))) {
            // TODO: Cancelation?
            GGL_LOGD("Job was canceled.");
            return GGL_ERR_OK;
        }
        if ((ggl_obj_into_i64(*remote_version) < 0)
            || (ggl_obj_into_i64(*remote_version) > INT32_MAX)) {
            GGL_LOGE(
                "Invalid version %" PRIi64 " received",
                ggl_obj_into_i64(*remote_version)
            );
            return GGL_ERR_FAILURE;
        }
        if ((int32_t) ggl_obj_into_i64(*remote_version) != local_version) {
            GGL_LOGD("Updating stale job status version number.");
            atomic_store_explicit(
                version,
                (int32_t) ggl_obj_into_i64(*remote_version),
                memory_order_release
            );
            local_version = (int32_t) ggl_obj_into_i64(*remote_version);
        }
        if (ggl_buffer_eq(job_status, ggl_obj_into_buf(*remote_status))) {
            GGL_LOGD("Job is already in the desired state.");
            break;
        }
        (void) ggl_sleep(1);
    }

    // save jobs ID and version to config in case of bootstrap
    ret = save_iot_jobs_id(job_id);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to save job ID to config.");
        return ret;
    }

    ret = save_iot_jobs_version(local_version);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to save job version to config.");
        return ret;
    }

    return GGL_ERR_OK;
}

static GglError describe_next_job(void *ctx) {
    (void) ctx;
    GGL_LOGD("Requesting next job information.");
    static uint8_t topic_scratch[512];
    GglBuffer topic = GGL_BUF(topic_scratch);
    GglError ret = create_get_next_job_topic(thing_name_buf, &topic);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // https://docs.aws.amazon.com/iot/latest/developerguide/jobs-mqtt-api.html
    GglObject payload_object = ggl_obj_map(GGL_MAP(
        { GGL_STR("jobId"), ggl_obj_buf(GGL_STR(NEXT_JOB_LITERAL)) },
        { GGL_STR("thingName"), ggl_obj_buf(thing_name_buf) },
        { GGL_STR("includeJobDocument"), ggl_obj_bool(true) },
        { GGL_STR("clientToken"), ggl_obj_buf(GGL_STR("jobs-nucleus-lite")) }
    ));

    static uint8_t response_scratch[4096];
    GglArena call_alloc = ggl_arena_init(GGL_BUF(response_scratch));
    GglObject job_description;
    ret = ggl_aws_iot_call(
        topic, payload_object, &call_alloc, &job_description
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to publish on describe job topic");
        return ret;
    }

    if (ggl_obj_type(job_description) != GGL_TYPE_MAP) {
        GGL_LOGE("Describe payload not of type Map");
        return GGL_ERR_FAILURE;
    }

    GglObject *execution = NULL;
    ret = ggl_map_validate(
        ggl_obj_into_map(job_description),
        GGL_MAP_SCHEMA(
            { GGL_STR("execution"), GGL_OPTIONAL, GGL_TYPE_MAP, &execution }
        )
    );
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }
    if (execution == NULL) {
        GGL_LOGD("No deployment to process.");
        return GGL_ERR_OK;
    }
    GGL_LOGD("Processing execution.");
    return process_job_execution(ggl_obj_into_map(*execution));
}

static GglError enqueue_job(GglMap deployment_doc, GglBuffer job_id) {
    GglError ret;
    {
        GGL_MTX_SCOPE_GUARD(&current_job_id_mutex);
        if (ggl_buffer_eq(current_job_id.buf, job_id)) {
            GGL_LOGI("Duplicate job document received. Skipping.");
            return GGL_ERR_OK;
        }

        current_job_version = 1;
        current_job_id = GGL_BYTE_VEC(current_job_id_buf);
        ret = ggl_byte_vec_append(&current_job_id, job_id);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Job ID too long.");
            return ret;
        }

        current_deployment_id = GGL_BYTE_VEC(current_deployment_id_buf);

        // TODO: backoff algorithm
        int64_t retries = 1;
        while (
            (ret = ggl_deployment_enqueue(
                 deployment_doc, &current_deployment_id, THING_GROUP_DEPLOYMENT
             ))
            == GGL_ERR_BUSY
        ) {
            int64_t sleep_for = 1 << MIN(7, retries);
            (void) ggl_sleep(sleep_for);
            ++retries;
        }
    }

    if (ret != GGL_ERR_OK) {
        (void) update_job(job_id, GGL_STR("FAILURE"), &current_job_version);
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
            { GGL_STR("jobId"), GGL_OPTIONAL, GGL_TYPE_BUF, &job_id },
            { GGL_STR("status"), GGL_OPTIONAL, GGL_TYPE_BUF, &status },
            { GGL_STR("jobDocument"),
              GGL_OPTIONAL,
              GGL_TYPE_MAP,
              &deployment_doc }
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
            { GGL_STR("QUEUED"), ggl_obj_i64(DSA_ENQUEUE_JOB) },
            { GGL_STR("IN_PROGRESS"), ggl_obj_i64(DSA_ENQUEUE_JOB) },
            { GGL_STR("SUCCEEDED"), ggl_obj_i64(DSA_DO_NOTHING) },
            { GGL_STR("FAILED"), ggl_obj_i64(DSA_DO_NOTHING) },
            { GGL_STR("TIMED_OUT"), ggl_obj_i64(DSA_CANCEL_JOB) },
            { GGL_STR("REJECTED"), ggl_obj_i64(DSA_DO_NOTHING) },
            { GGL_STR("REMOVED"), ggl_obj_i64(DSA_CANCEL_JOB) },
            { GGL_STR("CANCELED"), ggl_obj_i64(DSA_CANCEL_JOB) },
        );
        GglObject *integer = NULL;
        if (!ggl_map_get(
                status_action_map, ggl_obj_into_buf(*status), &integer
            )) {
            GGL_LOGE("Job status not a valid value");
            return GGL_ERR_INVALID;
        }
        action = (DeploymentStatusAction) ggl_obj_into_i64(*integer);
    }
    switch (action) {
    case DSA_CANCEL_JOB:
        // TODO: cancelation?
        break;

    case DSA_ENQUEUE_JOB: {
        if (deployment_doc == NULL) {
            GGL_LOGE(
                "Job status is queued/in progress, but no deployment doc was "
                "given."
            );
            return GGL_ERR_INVALID;
        }
        (void) enqueue_job(
            ggl_obj_into_map(*deployment_doc), ggl_obj_into_buf(*job_id)
        );
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
    GGL_LOGD("Received next job execution changed response.");
    static uint8_t subscription_scratch[4096];
    GglArena json_allocator = ggl_arena_init(GGL_BUF(subscription_scratch));
    GglObject json;
    GglError ret = deserialize_payload(&json_allocator, data, &json);
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }
    if (ggl_obj_type(json) != GGL_TYPE_MAP) {
        GGL_LOGE("JSON was not a map");
        return GGL_ERR_FAILURE;
    }

    GglObject *job_execution = NULL;
    ret = ggl_map_validate(
        ggl_obj_into_map(json),
        GGL_MAP_SCHEMA(
            { GGL_STR("execution"), GGL_OPTIONAL, GGL_TYPE_MAP, &job_execution }
        )
    );
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }
    if (job_execution == NULL) {
        // TODO: job cancelation
        return GGL_ERR_OK;
    }
    ret = process_job_execution(ggl_obj_into_map(*job_execution));
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}

noreturn void *job_listener_thread(void *ctx) {
    (void) ctx;
    ggl_backoff_indefinite(1, 1000, get_thing_name, NULL);
    listen_for_jobs_deployments();

    // coverity[infinite_loop]
    while (true) {
        {
            GGL_MTX_SCOPE_GUARD(&listener_mutex);
            while (!needs_describe) {
                pthread_cond_wait(&listener_cond, &listener_mutex);
            }
            needs_describe = false;
        }
        ggl_backoff_indefinite(10, 10000, describe_next_job, NULL);
    }
}

static void resubscribe_on_iotcored_close(void *ctx, uint32_t handle) {
    (void) ctx;
    (void) handle;
    GGL_LOGD("Subscriptions closed. Subscribing again.");
    listen_for_jobs_deployments();
}

static GglError subscribe_to_next_job_topics(void *ctx) {
    (void) ctx;
    static uint8_t topic_scratch[256];
    GglBuffer job_topic = GGL_BUF(topic_scratch);
    GglError err
        = create_next_job_execution_changed_topic(thing_name_buf, &job_topic);
    if (err != GGL_ERR_OK) {
        return err;
    }
    return ggl_aws_iot_mqtt_subscribe(
        GGL_BUF_LIST(job_topic),
        QOS_AT_LEAST_ONCE,
        next_job_execution_changed_callback,
        resubscribe_on_iotcored_close,
        NULL,
        NULL
    );
}

static GglError iot_jobs_on_reconnect(
    void *ctx, uint32_t handle, GglObject data
) {
    (void) ctx;
    (void) handle;
    if (ggl_obj_into_bool(data)) {
        GGL_LOGD("Reconnected to MQTT; requesting new job query publish.");
        GGL_MTX_SCOPE_GUARD(&listener_mutex);
        needs_describe = true;
        pthread_cond_signal(&listener_cond);
    }
    return GGL_ERR_OK;
}

static GglError subscribe_to_connection_status(void *ctx) {
    (void) ctx;
    return ggl_subscribe(
        GGL_STR("aws_iot_mqtt"),
        GGL_STR("connection_status"),
        GGL_MAP(),
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
    GGL_LOGD("Subscribing to IoT Jobs topics.");
    ggl_backoff_indefinite(10, 10000, subscribe_to_next_job_topics, NULL);
    ggl_backoff_indefinite(10, 10000, subscribe_to_connection_status, NULL);
}

GglError update_current_jobs_deployment(
    GglBuffer deployment_id, GglBuffer status
) {
    GglBuffer job_id = GGL_BUF((uint8_t[64]) { 0 });
    {
        GGL_MTX_SCOPE_GUARD(&current_job_id_mutex);
        if (!ggl_buffer_eq(deployment_id, current_deployment_id.buf)) {
            return GGL_ERR_NOENTRY;
        }
        memcpy(
            job_id.data, current_job_id.buf.data, current_deployment_id.buf.len
        );
        job_id.len = current_deployment_id.buf.len;
    }

    return update_job(job_id, status, &current_job_version);
}

GglError set_jobs_deployment_for_bootstrap(
    GglBuffer job_id, GglBuffer deployment_id, int64_t version
) {
    if ((version < 0) || (version > INT32_MAX)) {
        return GGL_ERR_INVALID;
    }
    GGL_MTX_SCOPE_GUARD(&current_job_id_mutex);
    if (!ggl_buffer_eq(job_id, current_job_id.buf)) {
        if (current_job_id.buf.len != 0) {
            GGL_LOGI("Bootstrap deployment was canceled by cloud.");
            return GGL_ERR_NOENTRY;
        }
        current_job_id = GGL_BYTE_VEC(current_job_id_buf);
        GglError ret = ggl_byte_vec_append(&current_job_id, job_id);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Job ID too long.");
            return ret;
        }
        current_deployment_id = GGL_BYTE_VEC(current_deployment_id_buf);
        ret = ggl_byte_vec_append(&current_deployment_id, deployment_id);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Deployment ID too long.");
            return ret;
        }
    }
    atomic_store_explicit(
        &current_job_version, (int32_t) version, memory_order_release
    );
    return GGL_ERR_OK;
}
