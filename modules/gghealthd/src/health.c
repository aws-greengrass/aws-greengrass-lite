// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "health.h"
#include "bus_client.h"
#include "sd_bus.h"
#include "subscriptions.h"
#include <assert.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/list.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/vector.h>
#include <ggl/exec.h>
#include <ggl/nucleus/constants.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

GgError gghealthd_get_status(GgBuffer component_name, GgBuffer *status) {
    assert(status != NULL);
    if (component_name.len > GGL_COMPONENT_NAME_MAX_LEN) {
        GG_LOGE("component_name too long");
        return GG_ERR_RANGE;
    }

    sd_bus *bus = NULL;
    GgError err = open_bus(&bus);
    GG_CLEANUP(sd_bus_unrefp, bus);

    if (gg_buffer_eq(component_name, GG_STR("gghealthd"))) {
        if (err == GG_ERR_OK) {
            *status = GG_STR("RUNNING");
        } else if (err == GG_ERR_NOCONN) {
            *status = GG_STR("ERRORED");
        } else if (err == GG_ERR_FATAL) {
            *status = GG_STR("BROKEN");
        }
        // successfully report own status even if unable to connect to
        // orchestrator
        return GG_ERR_OK;
    }

    if (err != GG_ERR_OK) {
        return err;
    }

    // only relay lifecycle state for configured components
    err = verify_component_exists(component_name);
    if (err != GG_ERR_OK) {
        return err;
    }

    uint8_t qualified_name[SERVICE_NAME_MAX_LEN + 1] = { 0 };
    err = get_service_name(component_name, &GG_BUF(qualified_name));
    if (err != GG_ERR_OK) {
        return GG_ERR_FAILURE;
    }

    sd_bus_message *reply = NULL;
    const char *unit_path = NULL;
    err = get_unit_path(bus, (char *) qualified_name, &reply, &unit_path);
    if (err != GG_ERR_OK) {
        return GG_ERR_FAILURE;
    }
    GG_CLEANUP(sd_bus_message_unrefp, reply);
    return get_lifecycle_state(bus, unit_path, status);
}

GgError gghealthd_update_status(GgBuffer component_name, GgBuffer status) {
    const GgMap STATUS_MAP = GG_MAP(
        gg_kv(GG_STR("NEW"), GG_OBJ_NULL),
        gg_kv(GG_STR("INSTALLED"), GG_OBJ_NULL),
        gg_kv(GG_STR("STARTING"), gg_obj_buf(GG_STR("--reloading"))),
        gg_kv(GG_STR("RUNNING"), gg_obj_buf(GG_STR("--ready"))),
        gg_kv(GG_STR("ERRORED"), GG_OBJ_NULL),
        gg_kv(GG_STR("BROKEN"), GG_OBJ_NULL),
        gg_kv(GG_STR("STOPPING"), gg_obj_buf(GG_STR("--stopping"))),
        gg_kv(GG_STR("FINISHED"), GG_OBJ_NULL)
    );

    GgObject *status_obj = NULL;
    if (!gg_map_get(STATUS_MAP, status, &status_obj)) {
        GG_LOGE("Invalid lifecycle_state");
        return GG_ERR_INVALID;
    }

    uint8_t qualified_name[SERVICE_NAME_MAX_LEN + 1] = { 0 };
    GgBuffer qualified_name_buf = GG_BUF(qualified_name);

    GgError err = verify_component_exists(component_name);
    if (err != GG_ERR_OK) {
        return err;
    }

    err = get_service_name(component_name, &qualified_name_buf);
    if (err != GG_ERR_OK) {
        return err;
    }

    sd_bus *bus = NULL;
    err = open_bus(&bus);
    GG_CLEANUP(sd_bus_unrefp, bus);
    if (err != GG_ERR_OK) {
        return err;
    }

    if (gg_obj_type(*status_obj) == GG_TYPE_NULL) {
        return GG_ERR_OK;
    }

    GgByteVec cgroup = GG_BYTE_VEC((uint8_t[128]) { 0 });
    gg_byte_vec_chain_append(&err, &cgroup, GG_STR("pids:/system.slice/"));
    gg_byte_vec_chain_append(&err, &cgroup, qualified_name_buf);
    gg_byte_vec_chain_push(&err, &cgroup, '\0');
    if (err != GG_ERR_OK) {
        return GG_ERR_FAILURE;
    }

    const char *argv[] = { "cgexec",
                           "-g",
                           (char *) cgroup.buf.data,
                           "--",
                           "systemd-notify",
                           (char *) gg_obj_into_buf(*status_obj).data,
                           NULL };
    err = ggl_exec_command(argv);
    if (err != GG_ERR_OK) {
        GG_LOGE("Failed to notify status");
    }

    GG_LOGD(
        "Component %.*s reported state updating to %.*s (%s)",
        (int) component_name.len,
        (const char *) component_name.data,
        (int) status.len,
        status.data,
        gg_obj_into_buf(*status_obj).data
    );

    return GG_ERR_OK;
}

GgError gghealthd_get_health(GgBuffer *status) {
    assert(status != NULL);

    sd_bus *bus = NULL;
    GgError err = open_bus(&bus);
    GG_CLEANUP(sd_bus_unrefp, bus);
    if (err != GG_ERR_OK) {
        *status = GG_STR("UNHEALTHY");
        return GG_ERR_OK;
    }

    // TODO: check all root components
    *status = GG_STR("HEALTHY");
    return GG_ERR_OK;
}

GgError gghealthd_restart_component(GgBuffer component_name) {
    if (component_name.len > GGL_COMPONENT_NAME_MAX_LEN) {
        GG_LOGE("component_name too long");
        return GG_ERR_RANGE;
    }

    sd_bus *bus = NULL;
    GgError err = open_bus(&bus);
    GG_CLEANUP(sd_bus_unrefp, bus);
    if (err != GG_ERR_OK) {
        return err;
    }

    err = verify_component_exists(component_name);
    if (err != GG_ERR_OK) {
        return err;
    }

    uint8_t qualified_name[SERVICE_NAME_MAX_LEN + 1] = { 0 };
    err = get_service_name(component_name, &GG_BUF(qualified_name));
    if (err != GG_ERR_OK) {
        return GG_ERR_FAILURE;
    }

    // This is done before and after a restart. IPC requests do not count
    // towards the burst limit. Doing this beforehand allows a failed component
    // to restart.
    reset_restart_counters(bus, (char *) qualified_name);

    err = restart_component(bus, (char *) qualified_name);
    if (err != GG_ERR_OK) {
        return err;
    }

    // Doing this again afterwards keeps restart counter at zero.
    reset_restart_counters(bus, (char *) qualified_name);

    GG_LOGI(
        "Successfully restarted component %.*s",
        (int) component_name.len,
        component_name.data
    );
    return GG_ERR_OK;
}

static bool is_nucleus_component(GgBuffer component_name) {
    if (gg_buffer_eq(component_name, GG_STR("DeploymentService"))) {
        return true;
    }
    if (gg_buffer_eq(component_name, GG_STR("FleetStatusService"))) {
        return true;
    }
    if (gg_buffer_eq(component_name, GG_STR("UpdateSystemPolicyService"))) {
        return true;
    }
    if (gg_buffer_eq(component_name, GG_STR("TelemetryAgent"))) {
        return true;
    }
    if (gg_buffer_eq(component_name, GG_STR("main"))) {
        return true;
    }
    if (gg_buffer_eq(
            component_name, GG_STR("aws.greengrass.fleet_provisioning")
        )) {
        return true;
    }
    return is_nucleus_component_type(component_name);
}

static void reset_failed_components(void) {
    static uint8_t component_list_buf[4096];
    GgArena alloc = gg_arena_init(GG_BUF(component_list_buf));
    GgList components;
    GgError ret = get_root_component_list(&alloc, &components);
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to get component list.");
        return;
    }

    sd_bus *bus = NULL;
    ret = open_bus(&bus);
    GG_CLEANUP(sd_bus_unrefp, bus);
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to connect to dbus to reset restart counters.");
        return;
    }

    size_t reset_count = 0;
    GG_LIST_FOREACH (component, components) {
        GgBuffer component_name = gg_obj_into_buf(*component);
        if (is_nucleus_component(component_name)) {
            continue;
        }
        uint8_t qualified_name[SERVICE_NAME_MAX_LEN + 1] = { 0 };
        ret = get_service_name(component_name, &GG_BUF(qualified_name));
        if (ret != GG_ERR_OK) {
            continue;
        }
        ++reset_count;
        reset_restart_counters(bus, (char *) qualified_name);
    }
    GG_LOGD("Processed reset-failed for %zu components", reset_count);
}

GgError gghealthd_init(void) {
    reset_failed_components();
    sd_notify(0, "READY=1");
    init_health_events();
    return GG_ERR_OK;
}
