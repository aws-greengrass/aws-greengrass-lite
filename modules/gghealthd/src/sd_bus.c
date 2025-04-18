// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sd_bus.h"
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <ggl/cleanup.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <inttypes.h>
#include <pthread.h>
#include <systemd/sd-bus.h>
#include <time.h>

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

GglError translate_dbus_call_error(int error) {
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

// bus must be freed via sd_bus_unrefp
GglError open_bus(sd_bus **bus) {
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

GglError get_unit_path(
    sd_bus *bus,
    const char *qualified_name,
    sd_bus_message **reply,
    const char **unit_path
) {
    assert((reply != NULL) && (*reply == NULL));

    sd_bus_error error = SD_BUS_ERROR_NULL;
    int ret = sd_bus_call_method(
        bus,
        DEFAULT_DESTINATION,
        DEFAULT_PATH,
        MANAGER_INTERFACE,
        "LoadUnit",
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

    ret = sd_bus_message_read_basic(*reply, 'o', unit_path);
    if (ret < 0) {
        sd_bus_message_unrefp(reply);
        *reply = NULL;
        *unit_path = NULL;
        return GGL_ERR_FATAL;
    }
    GGL_LOGD("Unit Path: %s", *unit_path);

    return GGL_ERR_OK;
}

GglError get_service_name(GglBuffer component_name, GglBuffer *qualified_name) {
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
        GGL_LOGD("Service name: %s", qualified_name->data);
    }
    return ret;
}

static GglError get_component_result(
    sd_bus *bus, const char *unit_path, GglBuffer *state
) {
    assert((bus != NULL) && (unit_path != NULL) && (state != NULL));
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
    GGL_LOGD("Timestamp: %" PRIu64, timestamp);

    // if a component has not run, it is installed
    if (timestamp == 0) {
        *state = GGL_STR("INSTALLED");
        return GGL_ERR_OK;
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
    GGL_CLEANUP(sd_bus_error_free, error);
    if (ret < 0) {
        GGL_LOGE(
            "Unable to retrieve D-Bus NRestarts property (errno=%d)", -ret
        );
        return translate_dbus_call_error(ret);
    }
    GGL_LOGD("NRetries: %" PRIu32, n_retries);
    if (n_retries >= 3) {
        GGL_LOGE("Component is broken (Exceeded retry limit)");
        *state = GGL_STR("BROKEN");
        return GGL_ERR_OK;
    }

    char *result = NULL;
    ret = sd_bus_get_property_string(
        bus,
        DEFAULT_DESTINATION,
        unit_path,
        SERVICE_INTERFACE,
        "Result",
        &error,
        &result
    );
    GGL_CLEANUP(cleanup_free, result);
    GGL_CLEANUP(sd_bus_error_free, error);
    if (ret < 0) {
        GGL_LOGE(
            "Unable to retrieve D-Bus Unit Result property (errno=%d)", -ret
        );
        return translate_dbus_call_error(ret);
    }
    GGL_LOGD("Result: %s", result);

    GglBuffer result_buffer = ggl_buffer_from_null_term(result);
    if (ggl_buffer_eq(result_buffer, GGL_STR("success"))) {
        *state = GGL_STR("FINISHED");
        // hitting the start limit means too many repeated failures
    } else {
        *state = GGL_STR("ERRORED");
    }
    return GGL_ERR_OK;
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
    GGL_LOGD("ActiveState: %s", *active_state);
    return GGL_ERR_OK;
}

GglError get_lifecycle_state(
    sd_bus *bus, const char *unit_path, GglBuffer *state
) {
    assert((bus != NULL) && (unit_path != NULL) && (state != NULL));

    char *active_state = NULL;
    GglError err = get_active_state(bus, unit_path, &active_state);
    GGL_CLEANUP(cleanup_free, active_state);
    if (err != GGL_ERR_OK) {
        return err;
    }
    const GglMap STATUS_MAP = GGL_MAP(
        { GGL_STR("activating"), ggl_obj_buf(GGL_STR("STARTING")) },
        { GGL_STR("active"), ggl_obj_buf(GGL_STR("RUNNING")) },
        // `reloading` doesn't have any mapping to greengrass. It's an
        // active component whose systemd (not greengrass) configuration is
        // reloading
        { GGL_STR("reloading"), ggl_obj_buf(GGL_STR("RUNNING")) },
        { GGL_STR("deactivating"), ggl_obj_buf(GGL_STR("STOPPING")) },
        // inactive and failed are ambiguous
        { GGL_STR("inactive"), GGL_OBJ_NULL },
        { GGL_STR("failed"), GGL_OBJ_NULL },
    );

    GglBuffer key = ggl_buffer_from_null_term(active_state);
    GglObject *value = NULL;
    if (!ggl_map_get(STATUS_MAP, key, &value)) {
        // unreachable?
        GGL_LOGE("unknown D-Bus ActiveState");
        return GGL_ERR_FATAL;
    }
    if (ggl_obj_type(*value) == GGL_TYPE_BUF) {
        *state = ggl_obj_into_buf(*value);
        return GGL_ERR_OK;
    }

    // disambiguate `failed` and `inactive`
    err = get_component_result(bus, unit_path, state);
    return err;
}
