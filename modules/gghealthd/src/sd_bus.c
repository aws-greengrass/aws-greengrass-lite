// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sd_bus.h"
#include <assert.h>
#include <errno.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/vector.h>
#include <ggl/nucleus/constants.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/types.h>
#include <systemd/sd-bus.h>
#include <time.h>

static pthread_mutex_t connect_time_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct timespec first_connect_attempt;
static struct timespec last_connect_attempt;
#define CONNECT_FAILURE_TIMEOUT 30

// assumes locked
static GgError get_connect_error(void) {
    if ((first_connect_attempt.tv_sec == 0)
        && (first_connect_attempt.tv_nsec == 0)) {
        return GG_ERR_OK;
    }
    struct timespec diff = {
        .tv_sec = last_connect_attempt.tv_sec - first_connect_attempt.tv_sec,
        .tv_nsec = last_connect_attempt.tv_nsec - first_connect_attempt.tv_nsec
    };
    if (diff.tv_nsec < 0) {
        diff.tv_sec--;
    }
    if (diff.tv_sec >= CONNECT_FAILURE_TIMEOUT) {
        return GG_ERR_FATAL;
    }
    return GG_ERR_NOCONN;
}

static GgError report_connect_error(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    GG_MTX_SCOPE_GUARD(&connect_time_mutex);
    // first failure
    if ((first_connect_attempt.tv_sec == 0)
        && (first_connect_attempt.tv_nsec == 0)) {
        first_connect_attempt = now;
    }
    last_connect_attempt = now;
    return get_connect_error();
}

static void report_connect_success(void) {
    GG_MTX_SCOPE_GUARD(&connect_time_mutex);
    first_connect_attempt = (struct timespec) { 0 };
    last_connect_attempt = (struct timespec) { 0 };
}

GgError translate_dbus_call_error(int error) {
    if (error >= 0) {
        return GG_ERR_OK;
    }
    switch (error) {
    case -ENOTCONN:
    case -ECONNRESET:
        return GG_ERR_NOCONN;
    case -ENOMEM:
        return GG_ERR_NOMEM;
    case -ENOENT:
        return GG_ERR_NOENTRY;
    case -EPERM:
    case -EINVAL:
        return GG_ERR_FATAL;
    default:
        return GG_ERR_FAILURE;
    }
}

// bus must be freed via sd_bus_unrefp
GgError open_bus(sd_bus **bus) {
    assert((bus != NULL) && (*bus == NULL));
    int ret = sd_bus_default_system(bus);
    if (ret < 0) {
        GG_LOGE("Unable to open default system bus (errno=%d)", -ret);
        *bus = NULL;
        return report_connect_error();
    }

    report_connect_success();
    return GG_ERR_OK;
}

GgError get_unit_path(
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
    GG_CLEANUP(sd_bus_error_free, error);
    if (ret < 0) {
        *reply = NULL;
        GG_LOGE(
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
        return GG_ERR_FATAL;
    }
    GG_LOGD("Unit Path: %s", *unit_path);

    return GG_ERR_OK;
}

GgError get_service_name(GgBuffer component_name, GgBuffer *qualified_name) {
    assert(
        (component_name.data != NULL) && (qualified_name != NULL)
        && (qualified_name->data != NULL)
    );
    assert(qualified_name->len > SERVICE_NAME_MAX_LEN);
    if (component_name.len > GGL_COMPONENT_NAME_MAX_LEN) {
        GG_LOGE("component name too long");
        return GG_ERR_RANGE;
    }

    GgError ret = GG_ERR_OK;
    GgByteVec vec = gg_byte_vec_init(*qualified_name);
    gg_byte_vec_chain_append(&ret, &vec, GG_STR(SERVICE_PREFIX));
    gg_byte_vec_chain_append(&ret, &vec, component_name);
    gg_byte_vec_chain_append(&ret, &vec, GG_STR(SERVICE_SUFFIX));
    gg_byte_vec_chain_push(&ret, &vec, '\0');
    if (ret == GG_ERR_OK) {
        qualified_name->len = vec.buf.len - 1;
        GG_LOGD("Service name: %s", qualified_name->data);
    }
    return ret;
}

static GgError get_component_result(
    sd_bus *bus, const char *unit_path, GgBuffer *state
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
    GG_CLEANUP(sd_bus_error_free, error);
    if (ret < 0) {
        GG_LOGE(
            "Unable to retrieve Component last run timestamp (errno=%d) (name=%s) (message=%s)",
            -ret,
            error.name,
            error.message
        );
        return translate_dbus_call_error(ret);
    }
    GG_LOGD("Timestamp: %" PRIu64, timestamp);

    // if a component has not run, it is installed
    if (timestamp == 0) {
        *state = GG_STR("INSTALLED");
        return GG_ERR_OK;
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
    GG_CLEANUP(sd_bus_error_free, error);
    if (ret < 0) {
        GG_LOGE("Unable to retrieve D-Bus NRestarts property (errno=%d)", -ret);
        return translate_dbus_call_error(ret);
    }
    GG_LOGD("NRetries: %" PRIu32, n_retries);
    if (n_retries >= 3) {
        GG_LOGE("Component is broken (Exceeded retry limit)");
        *state = GG_STR("BROKEN");
        return GG_ERR_OK;
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
    GG_CLEANUP(cleanup_free, result);
    GG_CLEANUP(sd_bus_error_free, error);
    if (ret < 0) {
        GG_LOGE(
            "Unable to retrieve D-Bus Unit Result property (errno=%d)", -ret
        );
        return translate_dbus_call_error(ret);
    }
    GG_LOGD("Result: %s", result);

    GgBuffer result_buffer = gg_buffer_from_null_term(result);
    if (gg_buffer_eq(result_buffer, GG_STR("success"))) {
        *state = GG_STR("FINISHED");
        // hitting the start limit means too many repeated failures
    } else {
        *state = GG_STR("ERRORED");
    }
    return GG_ERR_OK;
}

static GgError get_active_state(
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
    GG_CLEANUP(sd_bus_error_free, error);
    if (ret < 0) {
        GG_LOGE("Failed to read active state");
        return translate_dbus_call_error(ret);
    }
    GG_LOGD("ActiveState: %s", *active_state);
    return GG_ERR_OK;
}

GgError get_lifecycle_state(
    sd_bus *bus, const char *unit_path, GgBuffer *state
) {
    assert((bus != NULL) && (unit_path != NULL) && (state != NULL));

    char *active_state = NULL;
    GgError err = get_active_state(bus, unit_path, &active_state);
    GG_CLEANUP(cleanup_free, active_state);
    if (err != GG_ERR_OK) {
        return err;
    }
    const GgMap STATUS_MAP = GG_MAP(
        gg_kv(GG_STR("activating"), gg_obj_buf(GG_STR("STARTING"))),
        gg_kv(GG_STR("active"), gg_obj_buf(GG_STR("RUNNING"))),
        // `reloading` doesn't have any mapping to greengrass. It's an
        // active component whose systemd (not greengrass) configuration is
        // reloading
        gg_kv(GG_STR("reloading"), gg_obj_buf(GG_STR("RUNNING"))),
        gg_kv(GG_STR("deactivating"), gg_obj_buf(GG_STR("STOPPING"))),
        // inactive and failed are ambiguous
        gg_kv(GG_STR("inactive"), GG_OBJ_NULL),
        gg_kv(GG_STR("failed"), GG_OBJ_NULL),
    );

    GgBuffer key = gg_buffer_from_null_term(active_state);
    GgObject *value = NULL;
    if (!gg_map_get(STATUS_MAP, key, &value)) {
        // unreachable?
        GG_LOGE("unknown D-Bus ActiveState");
        return GG_ERR_FATAL;
    }
    if (gg_obj_type(*value) == GG_TYPE_BUF) {
        *state = gg_obj_into_buf(*value);
        return GG_ERR_OK;
    }

    // disambiguate `failed` and `inactive`
    err = get_component_result(bus, unit_path, state);
    return err;
}

void reset_restart_counters(sd_bus *bus, const char *qualified_name) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    GG_LOGT("Issuing systemctl reset-failed for %s", qualified_name);
    int ret = sd_bus_call_method(
        bus,
        "org.freedesktop.systemd1",
        "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager",
        "ResetFailedUnit",
        &error,
        &reply,
        "s",
        qualified_name
    );
    if (ret < 0) {
        GG_LOGW(
            "Failed to reset failure counter for %s (errno=%d) (name=%s) (message=%s)",
            qualified_name,
            -ret,
            error.name,
            error.message
        );
    }
    GG_CLEANUP(sd_bus_error_free, error);
    GG_CLEANUP(sd_bus_message_unrefp, reply);
}

GgError restart_component(sd_bus *bus, const char *qualified_name) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int ret = sd_bus_call_method(
        bus,
        "org.freedesktop.systemd1",
        "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager",
        "RestartUnit",
        &error,
        &reply,
        "ss",
        (char *) qualified_name,
        "replace"
    );
    GG_CLEANUP(sd_bus_error_free, error);
    GG_CLEANUP(sd_bus_message_unrefp, reply);

    if (ret < 0) {
        GG_LOGE(
            "Failed to restart component %s (errno=%d) (name=%s) (message=%s)",
            qualified_name,
            -ret,
            error.name,
            error.message
        );
        return translate_dbus_call_error(ret);
    }
    return GG_ERR_OK;
}
