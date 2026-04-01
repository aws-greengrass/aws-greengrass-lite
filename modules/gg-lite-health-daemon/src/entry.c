// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "gg-lite-health-daemon/config.h"
#include <gg/error.h>
#include <gg/log.h>
#include <gg/utils.h>
#include <gg_lite_health_daemon.h>
#include <signal.h>
#include <stddef.h>

static volatile sig_atomic_t running = 1;

static void handle_signal(int sig) {
    (void) sig;
    running = 0;
}

GgError run_gg_lite_health_daemon(void) {
    GG_LOGI("Started gg-lite-health-daemon.");

    HealthDaemonConfig config;
    GgError ret = config_init(&config);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = config_load(&config);
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to load config, using defaults.");
    }

    struct sigaction act = { .sa_handler = handle_signal };
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGINT, &act, NULL);

    while (running) {
        (void) gg_sleep(config.collection_interval_sec);
    }

    GG_LOGI("Shutting down gg-lite-health-daemon.");
    return GG_ERR_OK;
}
