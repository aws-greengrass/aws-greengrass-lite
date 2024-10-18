// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "health.h"
#include "bus_client.h"
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <inttypes.h>
#include <pthread.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>
#include <time.h>
#include <stdlib.h>

#define SERVICE_PREFIX "ggl."
#define SERVICE_PREFIX_LEN (sizeof(SERVICE_PREFIX) - 1U)
#define SERVICE_SUFFIX ".service"
#define SERVICE_SUFFIX_LEN (sizeof(SERVICE_SUFFIX) - 1U)
#define SERVICE_NAME_MAX_LEN \
    (SERVICE_PREFIX_LEN + COMPONENT_NAME_MAX_LEN + SERVICE_SUFFIX_LEN)

// destinations
#define DEFAULT_DESTINATION "org.freedesktop.systemd1"

// paths
#define DEFAULT_PATH "/org/freedesktop/systemd1"

// interfaces
#define MANAGER_INTERFACE "org.freedesktop.systemd1.Manager"
#define SERVICE_INTERFACE "org.freedesktop.systemd1.Service"
#define UNIT_INTERFACE "org.freedesktop.systemd1.Unit"

static GglError translate_dbus_call_error(int error) {
    if (error >= 0) {
        return GGL_ERR_OK;
    }
    switch (error) {
    case -ENOTCONN:
    case -ECONNRESET:
        return GGL_ERR_NOCONN;
    case -ENOMEM:
        return GGL_ERR_NOMEM;
    case -ENOENT:
        return GGL_ERR_NOENTRY;
    case -EPERM:
    case -EINVAL:
        return GGL_ERR_FATAL;
    default:
        return GGL_ERR_FAILURE;
    }
}

static pthread_mutex_t connect_time_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct timespec first_connect_attempt;
static struct timespec last_connect_attempt;
#define CONNECT_FAILURE_TIMEOUT 30

// assumes locked
static GglError get_connect_error(void) {
    if ((first_connect_attempt.tv_sec == 0)
        && (first_connect_attempt.tv_nsec == 0)) {
        return GGL_ERR_OK;
    }
    struct timespec diff = {
        .tv_sec = last_connect_attempt.tv_sec - first_connect_attempt.tv_sec,
        .tv_nsec = last_connect_attempt.tv_nsec - first_connect_attempt.tv_nsec
    };
    if (diff.tv_nsec < 0) {
        diff.tv_sec--;
    }
    if (diff.tv_sec >= CONNECT_FAILURE_TIMEOUT) {
        return GGL_ERR_FATAL;
    }
    return GGL_ERR_NOCONN;
}

static GglError report_connect_error(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    GGL_MTX_SCOPE_GUARD(&connect_time_mutex);
    // first failure
    if ((first_connect_attempt.tv_sec == 0)
        && (first_connect_attempt.tv_nsec == 0)) {
        first_connect_attempt = now;
    }
    last_connect_attempt = now;
    return get_connect_error();
}

static void report_connect_success(void) {
    GGL_MTX_SCOPE_GUARD(&connect_time_mutex);
    first_connect_attempt = (struct timespec) { 0 };
    last_connect_attempt = (struct timespec) { 0 };
}

// bus must be freed via sd_bus_unref
static GglError open_bus(sd_bus **bus) {
    assert((bus != NULL) && (*bus == NULL));
    int ret = sd_bus_default_system(bus);
    if (ret < 0) {
        GGL_LOGE("Unable to open default system bus (errno=%d)", -ret);
        *bus = NULL;
        return report_connect_error();
    }

    report_connect_success();
    return GGL_ERR_OK;
}

//  reply must be freed via sd_bus_message_unrefp
static GglError get_unit_path(
    sd_bus *bus, const char *qualified_name, sd_bus_message **reply
) {
    assert((reply != NULL) && (*reply == NULL));

    sd_bus_error error = SD_BUS_ERROR_NULL;
    int ret = sd_bus_call_method(
        bus,
        DEFAULT_DESTINATION,
        DEFAULT_PATH,
        MANAGER_INTERFACE,
        "GetUnit",
        &error,
        reply,
        "s",
        qualified_name
    );
    GGL_CLEANUP(sd_bus_error_free, error);

    if (ret < 0) {
        *reply = NULL;
        GGL_LOGE(
            "Unable to find Component (errno=%d) (name=%s) (message=%s)",
            -ret,
            error.name,
            error.message
        );

        return translate_dbus_call_error(ret);
    }

    return GGL_ERR_OK;
}

static GglError get_component_result(
    sd_bus *bus, const char *unit_path, GglBuffer *status
) {
    assert((bus != NULL) && (unit_path != NULL) && (status != NULL));
    uint64_t timestamp = 0;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int ret = sd_bus_get_property_trivial(
        bus,
        DEFAULT_DESTINATION,
        unit_path,
        UNIT_INTERFACE,
        "InactiveEnterTimestampMonotonic",
        &error,
        't',
        &timestamp
    );
    GGL_CLEANUP(sd_bus_error_free, error);
    if (ret < 0) {
        GGL_LOGE(
            "Unable to retrieve Component last run timestamp (errno=%d) "
            "(name=%s) (message=%s)",
            -ret,
            error.name,
            error.message
        );
        return translate_dbus_call_error(ret);
    }

    // if a component has not run, it is installed
    if (timestamp == 0) {
        *status = GGL_STR("INSTALLED");
    }

    uint32_t n_retries = 0;
    ret = sd_bus_get_property_trivial(
        bus,
        DEFAULT_DESTINATION,
        unit_path,
        SERVICE_INTERFACE,
        "NRestarts",
        &error,
        'u',
        &n_retries
    );
    if (ret < 0) {
        GGL_LOGE(
            "Unable to retrieve D-Bus NRestarts property (errno=%d)", -ret
        );
        return translate_dbus_call_error(ret);
    }
    GGL_LOGD("NRetries: %" PRIu32, n_retries);
    if (n_retries >= 3) {
        GGL_LOGE("Component is broken (Exceeded retry limit)");
        *status = GGL_STR("BROKEN");
        return GGL_ERR_OK;
    }

    char *result = NULL;
    error = SD_BUS_ERROR_NULL;
    ret = sd_bus_get_property_string(
        bus,
        DEFAULT_DESTINATION,
        unit_path,
        SERVICE_INTERFACE,
        "Result",
        &error,
        &result
    );
    GGL_CLEANUP(sd_bus_error_free, error);
    GGL_CLEANUP(cleanup_free, result);
    if (ret < 0) {
        GGL_LOGE(
            "Unable to retrieve D-Bus Unit Result property (errno=%d)", -ret
        );
        return translate_dbus_call_error(ret);
    }

    GglBuffer result_buffer = ggl_buffer_from_null_term(result);
    if (ggl_buffer_eq(result_buffer, GGL_STR("success"))) {
        *status = GGL_STR("FINISHED");
    } else {
        *status = GGL_STR("ERRORED");
    }
    return GGL_ERR_OK;
}

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
    GglError err = get_unit_path(bus, qualified_name, &reply);
    GGL_CLEANUP(sd_bus_message_unrefp, reply);
    if (err != GGL_ERR_OK) {
        return err;
    }

    const char *unit_path = NULL;
    int ret = sd_bus_message_read_basic(reply, 'o', &unit_path);

    if (ret < 0) {
        return GGL_ERR_FATAL;
    }

    sd_bus_error error = SD_BUS_ERROR_NULL;
    ret = sd_bus_get_property_string(
        bus, DEFAULT_DESTINATION, unit_path, interface, property, &error, value
    );
    GGL_CLEANUP(sd_bus_error_free, error);
    if (ret < 0) {
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

static GglError get_service_name(
    GglBuffer component_name, GglBuffer *qualified_name
) {
    assert(
        (component_name.data != NULL) && (qualified_name != NULL)
        && (qualified_name->data != NULL)
    );
    assert(qualified_name->len > SERVICE_NAME_MAX_LEN);
    if (component_name.len > COMPONENT_NAME_MAX_LEN) {
        GGL_LOGE("component name too long");
        return GGL_ERR_RANGE;
    }

    GglError ret = GGL_ERR_OK;
    GglByteVec vec = ggl_byte_vec_init(*qualified_name);
    ggl_byte_vec_chain_append(&ret, &vec, GGL_STR(SERVICE_PREFIX));
    ggl_byte_vec_chain_append(&ret, &vec, component_name);
    ggl_byte_vec_chain_append(&ret, &vec, GGL_STR(SERVICE_SUFFIX));
    ggl_byte_vec_chain_push(&ret, &vec, '\0');
    if (ret == GGL_ERR_OK) {
        qualified_name->len = vec.buf.len - 1;
    }
    return ret;
}

static GglError get_active_state(
    sd_bus *bus, const char *unit_path, char **active_state
) {
    assert((bus != NULL) && (unit_path != NULL) && (active_state != NULL));
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int ret = sd_bus_get_property_string(
        bus,
        DEFAULT_DESTINATION,
        unit_path,
        UNIT_INTERFACE,
        "ActiveState",
        &error,
        active_state
    );
    GGL_CLEANUP(sd_bus_error_free, error);
    if (ret < 0) {
        GGL_LOGE("Failed to read active state");
        return translate_dbus_call_error(ret);
    }
    return GGL_ERR_OK;
}

static GglError get_run_status(
    sd_bus *bus, const char *qualified_name, GglBuffer *status
) {
    assert((bus != NULL) && (qualified_name != NULL) && (status != NULL));
    sd_bus_message *reply = NULL;
    GglError err = get_unit_path(bus, qualified_name, &reply);
    GGL_CLEANUP(sd_bus_message_unrefp, reply);
    if (err != GGL_ERR_OK) {
        return err;
    }
    char *unit_path = NULL;
    int ret = sd_bus_message_read_basic(reply, 'o', &unit_path);
    if (ret < 0) {
        GGL_LOGE("failed to read unit path");
        return GGL_ERR_FATAL;
    }

    char *active_state = NULL;
    err = get_active_state(bus, unit_path, &active_state);
    GGL_CLEANUP(cleanup_free, active_state);
    if (err != GGL_ERR_OK) {
        return err;
    }
    const GglMap STATUS_MAP = GGL_MAP(
        { GGL_STR("activating"), GGL_OBJ_BUF(GGL_STR("STARTING")) },
        { GGL_STR("active"), GGL_OBJ_BUF(GGL_STR("RUNNING")) },
        // `reloading` doesn't have any mapping to greengrass. It's an
        // active component whose systemd (not greengrass) configuration is
        // reloading
        { GGL_STR("reloading"), GGL_OBJ_BUF(GGL_STR("RUNNING")) },
        { GGL_STR("deactivating"), GGL_OBJ_BUF(GGL_STR("STOPPING")) },
        // inactive and failed are ambiguous
        { GGL_STR("inactive"), GGL_OBJ_NULL() },
        { GGL_STR("failed"), GGL_OBJ_NULL() },
    );

    GglBuffer key = ggl_buffer_from_null_term(active_state);
    GglObject *value = NULL;
    if (!ggl_map_get(STATUS_MAP, key, &value)) {
        // unreachable?
        GGL_LOGE("unknown D-Bus ActiveState");
        return GGL_ERR_FATAL;
    }
    if (value->type == GGL_TYPE_BUF) {
        *status = value->buf;
        return GGL_ERR_OK;
    }

    // disambiguate `failed` and `inactive`
    err = get_component_result(bus, unit_path, status);
    return err;
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
        return err;
    }
    return get_run_status(bus, (const char *) qualified_name, status);
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

GglError gghealthd_init(void) {
    return GGL_ERR_OK;
}
