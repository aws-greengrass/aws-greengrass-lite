// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "health.h"
#include "bus_client.h"
#include "sd_bus.h"
#include "subscriptions.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/exec.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/nucleus/constants.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>
#include <stdint.h>
#include <stdlib.h>

GglError gghealthd_get_status(GglBuffer component_name, GglBuffer *status) {
    assert(status != NULL);
    if (component_name.len > GGL_COMPONENT_NAME_MAX_LEN) {
        GGL_LOGE("component_name too long");
        return GGL_ERR_RANGE;
    }

    sd_bus *bus = NULL;
    GglError err = open_bus(&bus);
    GGL_CLEANUP(sd_bus_unrefp, bus);

    if (ggl_buffer_eq(component_name, GGL_STR("gghealthd"))) {
        if (err == GGL_ERR_OK) {
            *status = GGL_STR("RUNNING");
        } else if (err == GGL_ERR_NOCONN) {
            *status = GGL_STR("ERRORED");
        } else if (err == GGL_ERR_FATAL) {
            *status = GGL_STR("BROKEN");
        }
        // successfully report own status even if unable to connect to
        // orchestrator
        return GGL_ERR_OK;
    }

    if (err != GGL_ERR_OK) {
        return err;
    }

    // only relay lifecycle state for configured components
    err = verify_component_exists(component_name);
    if (err != GGL_ERR_OK) {
        return err;
    }

    uint8_t qualified_name[SERVICE_NAME_MAX_LEN + 1] = { 0 };
    err = get_service_name(component_name, &GGL_BUF(qualified_name));
    if (err != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }

    sd_bus_message *reply = NULL;
    const char *unit_path = NULL;
    err = get_unit_path(bus, (char *) qualified_name, &reply, &unit_path);
    if (err != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }
    GGL_CLEANUP(sd_bus_message_unrefp, reply);
    return get_lifecycle_state(bus, unit_path, status);
}

GglError gghealthd_update_status(GglBuffer component_name, GglBuffer status) {
    const GglMap STATUS_MAP = GGL_MAP(
        ggl_kv(GGL_STR("NEW"), GGL_OBJ_NULL),
        ggl_kv(GGL_STR("INSTALLED"), GGL_OBJ_NULL),
        ggl_kv(GGL_STR("STARTING"), ggl_obj_buf(GGL_STR("--reloading"))),
        ggl_kv(GGL_STR("RUNNING"), ggl_obj_buf(GGL_STR("--ready"))),
        ggl_kv(GGL_STR("ERRORED"), GGL_OBJ_NULL),
        ggl_kv(GGL_STR("BROKEN"), GGL_OBJ_NULL),
        ggl_kv(GGL_STR("STOPPING"), ggl_obj_buf(GGL_STR("--stopping"))),
        ggl_kv(GGL_STR("FINISHED"), GGL_OBJ_NULL)
    );

    GglObject *status_obj = NULL;
    if (!ggl_map_get(STATUS_MAP, status, &status_obj)) {
        GGL_LOGE("Invalid lifecycle_state");
        return GGL_ERR_INVALID;
    }

    uint8_t qualified_name[SERVICE_NAME_MAX_LEN + 1] = { 0 };
    GglBuffer qualified_name_buf = GGL_BUF(qualified_name);

    GglError err = verify_component_exists(component_name);
    if (err != GGL_ERR_OK) {
        return err;
    }

    err = get_service_name(component_name, &qualified_name_buf);
    if (err != GGL_ERR_OK) {
        return err;
    }

    sd_bus *bus = NULL;
    err = open_bus(&bus);
    GGL_CLEANUP(sd_bus_unrefp, bus);
    if (err != GGL_ERR_OK) {
        return err;
    }

    if (ggl_obj_type(*status_obj) == GGL_TYPE_NULL) {
        return GGL_ERR_OK;
    }

    GglByteVec cgroup = GGL_BYTE_VEC((uint8_t[128]) { 0 });
    ggl_byte_vec_chain_append(&err, &cgroup, GGL_STR("pids:/system.slice/"));
    ggl_byte_vec_chain_append(&err, &cgroup, qualified_name_buf);
    ggl_byte_vec_chain_push(&err, &cgroup, '\0');
    if (err != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }

    const char *argv[] = { "cgexec",
                           "-g",
                           (char *) cgroup.buf.data,
                           "--",
                           "systemd-notify",
                           (char *) ggl_obj_into_buf(*status_obj).data,
                           NULL };
    err = ggl_exec_command(argv);
    if (err != GGL_ERR_OK) {
        GGL_LOGE("Failed to notify status");
    }

    GGL_LOGD(
        "Component %.*s reported state updating to %.*s (%s)",
        (int) component_name.len,
        (const char *) component_name.data,
        (int) status.len,
        status.data,
        ggl_obj_into_buf(*status_obj).data
    );

    return GGL_ERR_OK;
}

GglError gghealthd_get_health(GglBuffer *status) {
    assert(status != NULL);

    sd_bus *bus = NULL;
    GglError err = open_bus(&bus);
    GGL_CLEANUP(sd_bus_unrefp, bus);
    if (err != GGL_ERR_OK) {
        *status = GGL_STR("UNHEALTHY");
        return GGL_ERR_OK;
    }

    // TODO: check all root components
    *status = GGL_STR("HEALTHY");
    return GGL_ERR_OK;
}

GglError gghealthd_init(void) {
    sd_notify(0, "READY=1");
    init_health_events();
    return GGL_ERR_OK;
}
