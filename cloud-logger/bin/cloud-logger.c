// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <cloud_logger.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/utils.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define UPLOAD_MAX_LINES 5
#define UPLOAD_MAX_BUFFER 2048 * UPLOAD_MAX_LINES

typedef struct {
    uint8_t mem[UPLOAD_MAX_BUFFER];
    GglObject ids_array[UPLOAD_MAX_LINES];
    GglObjVec upload;
} MEMORY;

MEMORY space_one
    = { .mem = { 0 },
        .ids_array = {},
        .upload = { .list = { .items = space_one.ids_array, .len = 0 },
                    .capacity = (size_t) UPLOAD_MAX_LINES } };

MEMORY space_two
    = { .mem = { 0 },
        .ids_array = {},
        .upload = { .list = { .items = space_one.ids_array, .len = 0 },
                    .capacity = (size_t) UPLOAD_MAX_LINES } };

MEMORY *filling = &space_one;
MEMORY *draining = NULL;

sem_t drain;

static void *drain_logs_thread(void *args) {
    (void) args;
    while (1) {
        sem_wait(&drain);

        MEMORY *current = draining;

        for (size_t index = 0; index < current->upload.list.len; index++) {
            // TODO:: Replace with upload logic
            printf(
                "%.*s",
                (int) current->upload.list.items[index].buf.len,
                (char *) current->upload.list.items[index].buf.data
            );

            // TODO: Find a better way to drain early if fill is complete
        }
    }
    return NULL;
}

static void *read_logs_thread(void *args) {
    (void) args;
    // Command to fetch all journalctl logs
    const char *cmd = "journalctl -f";

    // Open a process by creating a pipe, fork(), and invoking the shell
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        GGL_LOGI("popen failed");
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
            GGL_LOGE("Something went wrong");
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
