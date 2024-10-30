// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "health.h"
#include "bus_client.h"
#include "sd_bus.h"
#include "subscriptions.h"
#include <sys/types.h>
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <pthread.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>
#include <stdint.h>
#include <stdlib.h>

// N.D: returned string must be freed by caller
static GglError get_property_string(
    sd_bus *bus,
    const char *qualified_name,
    char *interface,
    char *property,
    char **value
) {
    assert(bus != NULL);
    assert(qualified_name != NULL);
    assert(interface != NULL);
    assert(property != NULL);
    assert((value != NULL) && (*value == NULL));

    sd_bus_message *reply = NULL;
    const char *unit_path = NULL;
    GglError err = get_unit_path(bus, qualified_name, &reply, &unit_path);
    GGL_CLEANUP(sd_bus_message_unrefp, reply);
    if (err != GGL_ERR_OK) {
        return err;
    }

    sd_bus_error error = SD_BUS_ERROR_NULL;
    int sd_ret = sd_bus_get_property_string(
        bus, DEFAULT_DESTINATION, unit_path, interface, property, &error, value
    );
    GGL_CLEANUP(sd_bus_error_free, error);
    if (sd_ret < 0) {
        return GGL_ERR_FATAL;
    }
    return GGL_ERR_OK;
}

static GglError get_component_pid(
    sd_bus *bus, const char *component_name, int *pid
) {
    // TODO: there are MAIN_PID and CONTROL_PID properties. MAIN_PID is probably
    // sufficient for sd_pid_notify. Components probably won't have more than
    // one active processes.
    char *pid_string = NULL;
    GglError err = get_property_string(
        bus, component_name, SERVICE_INTERFACE, "MAIN_PID", &pid_string
    );
    GGL_CLEANUP(cleanup_free, pid_string);
    if (err != GGL_ERR_OK) {
        GGL_LOGE("Unable to acquire pid");
        *pid = -1;
        return err;
    }

    *pid = atoi(pid_string);
    if (*pid <= 0) {
        return GGL_ERR_NOENTRY;
    }

    return GGL_ERR_OK;
}

GglError gghealthd_get_status(GglBuffer component_name, GglBuffer *status) {
    assert(status != NULL);
    if (component_name.len > COMPONENT_NAME_MAX_LEN) {
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
        { GGL_STR("NEW"), GGL_OBJ_NULL() },
        { GGL_STR("INSTALLED"), GGL_OBJ_NULL() },
        { GGL_STR("STARTING"), GGL_OBJ_BUF(GGL_STR("RELOADING=1")) },
        { GGL_STR("RUNNING"), GGL_OBJ_BUF(GGL_STR("READY=1")) },
        { GGL_STR("ERRORED"), GGL_OBJ_BUF(GGL_STR("ERRNO=71")) },
        { GGL_STR("BROKEN"), GGL_OBJ_BUF(GGL_STR("ERRNO=71")) },
        { GGL_STR("STOPPING"), GGL_OBJ_BUF(GGL_STR("STOPPING=1")) },
        { GGL_STR("FINISHED"), GGL_OBJ_NULL() }
    );

    GglObject *obj = NULL;
    if (!ggl_map_get(STATUS_MAP, status, &obj)) {
        GGL_LOGE("Invalid lifecycle_state");
        return GGL_ERR_INVALID;
    }

    uint8_t qualified_name[SERVICE_NAME_MAX_LEN + 1] = { 0 };

    GglError err = verify_component_exists(component_name);
    if (err != GGL_ERR_OK) {
        return err;
    }

    err = get_service_name(component_name, &GGL_BUF(qualified_name));
    if (err != GGL_ERR_OK) {
        return err;
    }

    sd_bus *bus = NULL;
    err = open_bus(&bus);
    GGL_CLEANUP(sd_bus_unrefp, bus);
    if (err != GGL_ERR_OK) {
        return err;
    }

    if (obj->type == GGL_TYPE_NULL) {
        return GGL_ERR_OK;
    }

    int pid = 0;
    err = get_component_pid(bus, (const char *) qualified_name, &pid);
    if (err != GGL_ERR_OK) {
        return err;
    }

    int ret = sd_pid_notify(pid, 0, (const char *) obj->buf.data);
    if (ret < 0) {
        GGL_LOGE("Unable to update component state (errno=%d)", -ret);
        return GGL_ERR_FATAL;
    }
    GGL_LOGD(
        "Component %.*s reported state updating to %.*s\n",
        (int) component_name.len,
        (const char *) component_name.data,
        (int) status.len,
        status.data
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

static pthread_t event_thread;

GglError gghealthd_init(void) {
    pthread_create(&event_thread, NULL, health_event_loop_thread, NULL);
    return GGL_ERR_OK;
}
