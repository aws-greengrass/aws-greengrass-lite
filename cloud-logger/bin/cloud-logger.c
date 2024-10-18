// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <sys/types.h>
#include <cloud_logger.h>
#include <ggipc/client.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/constants.h>
#include <ggl/error.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define UPLOAD_MAX_LINES 50
#define UPLOAD_MAX_BUFFER (MAX_LINE_LENGTH * UPLOAD_MAX_LINES)

typedef struct {
    uint8_t mem[UPLOAD_MAX_BUFFER];
    GglObject ids_array[UPLOAD_MAX_LINES];
    GglObjVec upload;
} MEMORY;

static MEMORY space_one
    = { .mem = { 0 },
        .ids_array = {},
        .upload = { .list = { .items = space_one.ids_array, .len = 0 },
                    .capacity = (size_t) UPLOAD_MAX_LINES } };

static MEMORY space_two
    = { .mem = { 0 },
        .ids_array = {},
        .upload = { .list = { .items = space_one.ids_array, .len = 0 },
                    .capacity = (size_t) UPLOAD_MAX_LINES } };

static MEMORY *filling = &space_one;
static MEMORY *draining = NULL;

static sem_t drain;

static void *drain_logs_thread(void *args) {
    (void) args;

    // Get the Thing Name from Environment Variable
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    char *thing_name = getenv("AWS_IOT_THING_NAME");
    if (thing_name == NULL) {
        GGL_LOGE("Thing name env var not set.");
        return NULL;
    }

    uint8_t publish_topic
        [sizeof("gglite/") + THING_NAME_MAX_LENGTH
         + sizeof("/logs"
         )]; // TODO: Make this configurable and recommend a basic ingest topic.
             // Keep the authz in recipe.yml in sync
    GglByteVec publish_topic_vec = GGL_BYTE_VEC(publish_topic);
    GglError ret = ggl_byte_vec_append(&publish_topic_vec, GGL_STR("gglite/"));
    ggl_byte_vec_chain_append(
        &ret, &publish_topic_vec, ggl_buffer_from_null_term(thing_name)
    );
    ggl_byte_vec_chain_append(&ret, &publish_topic_vec, GGL_STR("/logs"));
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to construct publish topic");
        return NULL;
    }

    GGL_LOGI(
        "Will send log messages to topic: %.*s",
        (int) publish_topic_vec.buf.len,
        publish_topic_vec.buf.data
    );

    // Get the SocketPath from Environment Variable
    char *socket_path
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        = getenv("AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT");
    if (socket_path == NULL) {
        GGL_LOGE("IPC socket path env var not set.");
        return NULL;
    }

    // Fetch the SVCUID
    static uint8_t resp_mem[PATH_MAX];
    GglBuffer resp = GGL_BUF(resp_mem);
    resp.len = GGL_IPC_MAX_SVCUID_LEN;

    int conn = -1;
    ret = ggipc_connect_auth(
        ggl_buffer_from_null_term(socket_path), &resp, &conn
    );
    if (ret != GGL_ERR_OK) {
        return NULL;
    }

    while (1) {
        sem_wait(&drain);

        MEMORY *current = draining;

        for (size_t index = 0; index < current->upload.list.len; index++) {
            // TODO: validate that log messages are correct format (UTF8)
            static uint8_t
                json_encode_memory[MAX_LINE_LENGTH + 30]; // TODO: figure out
                                                          // the right size for
                                                          // most/all logs
            GglBuffer json_encoded_buf = GGL_BUF(json_encode_memory);
            ret = ggl_json_encode(
                GGL_OBJ_MAP(GGL_MAP(
                    { GGL_STR("message"),
                      GGL_OBJ_BUF(current->upload.list.items[index].buf) }
                )),
                &json_encoded_buf
            );

            static uint8_t memory_for_base64_encode
                [MAX_LINE_LENGTH * 2]; // TODO: figure out the right size for
                                       // most/all logs
            GglBumpAlloc base64_encode_alloc
                = ggl_bump_alloc_init(GGL_BUF(memory_for_base64_encode));
            ret = ggipc_publish_to_iot_core(
                conn,
                publish_topic_vec.buf,
                json_encoded_buf,
                0,
                &base64_encode_alloc.alloc
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "Failed to publish log with err: %s", ggl_strerror(ret)
                );
                continue;
            }

            // TODO: Find a better way to drain early if fill is complete
        }
    }
    return NULL;
}

static void *read_logs_thread(void *args) {
    (void) args;

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    setenv("TERM", "dumb", true);

    // Command to fetch all journalctl logs
    const char *cmd = "journalctl -f ";

    // Open a process by creating a pipe, fork(), and invoking the shell
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        GGL_LOGE("popen failed");
        return NULL;
    }

    while (1) {
        // Reset and reinitialize for reading fresh logs
        GglBumpAlloc mem_bump_alloc
            = ggl_bump_alloc_init(GGL_BUF(filling->mem));
        filling->upload.list.len = 0;

        // fetch the logs from journalctl
        GglError ret = read_log(fp, &filling->upload, &mem_bump_alloc.alloc);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Error reading from log: %s", ggl_strerror(ret));
            return NULL;
        }

        draining = filling;
        if (filling == &space_one) {
            filling = &space_two;
        } else {
            filling = &space_one;
        }

        sem_post(&drain);
    }

    return NULL;
}

int main(void) {
    sem_init(&drain, 0, 0);

    pthread_t read_thread = { 0 };
    int sys_ret = pthread_create(&read_thread, NULL, read_logs_thread, NULL);
    if (sys_ret != 0) {
        GGL_LOGE("Failed to create subscription response thread.");
        _Exit(1);
    }

    pthread_t drain_thread = { 0 };
    sys_ret = pthread_create(&drain_thread, NULL, drain_logs_thread, NULL);
    if (sys_ret != 0) {
        GGL_LOGE("Failed to create subscription response thread.");
        _Exit(1);
    }

    pthread_join(read_thread, NULL);
    pthread_join(drain_thread, NULL);

    sem_destroy(&drain);
}
