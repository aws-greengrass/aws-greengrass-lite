// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "subscriptions.h"
#include "sd_bus.h"
#include <assert.h>
#include <errno.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h> // IWYU pragma: keep (TODO: remove after file.h refactor)
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/utils.h>
#include <ggl/core_bus/server.h>
#include <ggl/nucleus/constants.h>
#include <ggl/socket_server.h>
#include <inttypes.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef GGHEALTHD_MAX_SUBSCRIPIONS
#define GGHEALTHD_MAX_SUBSCRIPTIONS 10
#endif

// SoA subscription layout
static sd_bus_slot *slots[GGHEALTHD_MAX_SUBSCRIPTIONS];
static uint32_t handles[GGHEALTHD_MAX_SUBSCRIPTIONS];
static size_t component_names_len[GGHEALTHD_MAX_SUBSCRIPTIONS];
static uint8_t component_names[GGHEALTHD_MAX_SUBSCRIPTIONS]
                              [GGL_COMPONENT_NAME_MAX_LEN];

static sd_bus *global_bus;

// Handle subscribed to all-component lifecycle state changes (broadcast topic).
// The design has a single consumer (ggdeploymentd) and the core-bus server
// already bounds the total number of client connections, so a single handle is
// sufficient; there is no per-subscriber systemd resource here (one global
// match feeds every subscriber). Accessed only from the socket-server thread
// (core-bus handlers and the sd_event callback), so no locking is needed.
static uint32_t broadcast_handle;

static GgBuffer component_name_buf(int index) {
    assert((index >= 0) && (index < GGHEALTHD_MAX_SUBSCRIPTIONS));
    return gg_buffer_substr(
        GG_BUF(component_names[index]), 0, component_names_len[index]
    );
}

// Event loop thread functions //

// Greengrass lifecycle states that gghealthd reports as "settled" to
// subscribers. Shared by the per-component and broadcast handlers.
static bool is_terminal_state(GgBuffer status) {
    return gg_buffer_eq(GG_STR("BROKEN"), status)
        || gg_buffer_eq(GG_STR("FINISHED"), status)
        || gg_buffer_eq(GG_STR("RUNNING"), status);
}

// Build the standard {component_name, lifecycle_state} response and send it to
// a subscriber. Shared by the per-component and broadcast handlers.
static void respond_state_change(
    uint32_t handle, GgBuffer component_name, GgBuffer status
) {
    ggl_sub_respond(
        handle,
        gg_obj_map(GG_MAP(
            gg_kv(GG_STR("component_name"), gg_obj_buf(component_name)),
            gg_kv(GG_STR("lifecycle_state"), gg_obj_buf(status))
        ))
    );
}

static int properties_changed_handler(
    sd_bus_message *m, void *user_data, sd_bus_error *ret_error
) {
    // index = &slots[index] - &slots[0]
    ptrdiff_t index = ((sd_bus_slot **) user_data) - &slots[0];
    if ((index < 0) || (index >= GGHEALTHD_MAX_SUBSCRIPTIONS)) {
        GG_LOGE("Bogus index retrieved.");
        sd_bus_error_set_errno(ret_error, -EINVAL);
        return -1;
    }
    if (slots[index] == NULL) {
        GG_LOGD("Signal received after unref.");
        return -1;
    }
    uint32_t handle = handles[index];
    if (handle == 0) {
        GG_LOGD("Signal received after handle closed.");
        return -1;
    }

    GgBuffer component_name = component_name_buf((int) index);
    sd_bus *bus = sd_bus_message_get_bus(m);
    if (bus == NULL) {
        GG_LOGW("No bus connection?");
    }

    const char *unit_path = sd_bus_message_get_path(m);
    if (unit_path == NULL) {
        GG_LOGD("Message has no path. Skipping signal.");
        return 0;
    }
    GG_LOGD("Properties changed for %s", unit_path);

    GgBuffer status = GG_STR("");
    GgError ret = get_lifecycle_state(bus, unit_path, &status);
    if (ret != GG_ERR_OK) {
        return -1;
    }

    // RUNNING, FINISHED, BROKEN,  terminal states
    if (is_terminal_state(status)) {
        GG_LOGI(
            "%.*s finished their lifecycle (status=%.*s)",
            (int) component_name.len,
            component_name.data,
            (int) status.len,
            status.data
        );
        respond_state_change(handle, component_name, status);
    } else {
        GG_LOGD("Signalled for non-terminal state. ");
    }

    return 0;
}

static GgError register_dbus_signal(int index) {
    GG_LOGD("Event loop thread enabling signal for %d.", index);
    uint8_t qualified_name_bytes[SERVICE_NAME_MAX_LEN + 1];
    GgBuffer qualified_name = GG_BUF(qualified_name_bytes);
    GgBuffer component_name = component_name_buf(index);
    GgError ret = get_service_name(component_name, &qualified_name);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    sd_bus_message *reply = NULL;
    const char *unit_path = NULL;
    ret = get_unit_path(
        global_bus, (const char *) qualified_name.data, &reply, &unit_path
    );
    GG_CLEANUP(sd_bus_message_unrefp, reply);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    sd_bus_slot *slot = NULL;
    int sd_err = sd_bus_match_signal(
        global_bus,
        &slot,
        NULL,
        unit_path,
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        properties_changed_handler,
        &slots[index]
    );
    if (sd_err < 0) {
        GG_LOGE(
            "Failed to match signal (unit=%s) (errno=%d)", unit_path, -sd_err
        );
        return translate_dbus_call_error(sd_err);
    }
    slots[index] = slot;
    GG_LOGD("Accepting subscription.");
    ggl_sub_accept(
        handles[index], gghealthd_unregister_lifecycle_subscription, NULL
    );
    return GG_ERR_OK;
}

static void unregister_dbus_signal(int index) {
    GG_LOGD("Event loop thread disabling signal for %d.", index);
    sd_bus_slot_unref(slots[index]);
    slots[index] = NULL;
    handles[index] = 0;
    component_names_len[index] = 0;
}

static sd_event *sd_event_ctx;

static void event_handle_callback(void) {
    GG_LOGD("Event handle callback.");
    int ret;
    while ((ret = sd_event_run(sd_event_ctx, 0)) > 0) { }
    GG_LOGD("Event loop returned %d.", ret);
}

// Returns true if the PropertiesChanged message body indicates that the
// systemd `ActiveState` property actually changed. systemd only lists a
// property in the changed/invalidated set when its value transitions, so this
// is the "filter on actual change" that dedupes redundant signals. Consumes
// the message body, which the caller does not otherwise read.
static bool active_state_changed(sd_bus_message *m) {
    // PropertiesChanged signature: s a{sv} as
    //   (interface_name, changed_properties, invalidated_properties)
    const char *iface = NULL;
    if (sd_bus_message_read_basic(m, 's', &iface) < 0) {
        return false;
    }

    bool found = false;

    if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0) {
        return false;
    }
    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
        const char *key = NULL;
        if (sd_bus_message_read_basic(m, 's', &key) < 0) {
            break;
        }
        if ((key != NULL) && (strcmp(key, "ActiveState") == 0)) {
            found = true;
        }
        // skip the variant value to advance to the next dict entry
        if (sd_bus_message_skip(m, "v") < 0) {
            break;
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);

    // also treat ActiveState being invalidated as a change
    if (sd_bus_message_enter_container(m, 'a', "s") >= 0) {
        const char *invalidated = NULL;
        while (sd_bus_message_read_basic(m, 's', &invalidated) > 0) {
            if ((invalidated != NULL)
                && (strcmp(invalidated, "ActiveState") == 0)) {
                found = true;
            }
        }
        sd_bus_message_exit_container(m);
    }

    return found;
}

// Kept referenced for the lifetime of the process so the match stays installed.
static sd_bus_slot *broadcast_match_slot;

// Single global PropertiesChanged handler feeding the broadcast subscription.
// Filters to Greengrass component units whose ActiveState actually changed,
// maps to a terminal Greengrass lifecycle state, and fans the result out to
// the broadcast subscriber.
static int global_properties_changed_handler(
    sd_bus_message *m, void *user_data, sd_bus_error *ret_error
) {
    (void) user_data;
    (void) ret_error;

    if (broadcast_handle == 0) {
        // no subscriber; nothing to fan out to
        return 0;
    }

    const char *unit_path = sd_bus_message_get_path(m);
    if (unit_path == NULL) {
        return 0;
    }

    // filter on actual ActiveState change
    if (!active_state_changed(m)) {
        return 0;
    }

    sd_bus *bus = sd_bus_message_get_bus(m);
    if (bus == NULL) {
        GG_LOGW("No bus connection on signal.");
        return 0;
    }

    // resolve and filter to ggl.* component units
    uint8_t name_bytes[GGL_COMPONENT_NAME_MAX_LEN];
    GgBuffer component_name = GG_BUF(name_bytes);
    GgError ret = get_component_name_from_unit(bus, unit_path, &component_name);
    if (ret != GG_ERR_OK) {
        // not a Greengrass component unit (or unreadable); ignore
        return 0;
    }

    // Ignore core nucleus services (ggl.core.*.service -> core.*); these are
    // internal daemons and can be skipped
    if (gg_buffer_has_prefix(component_name, GG_STR("core."))) {
        GG_LOGD(
            "Ignoring core service %.*s state change.",
            (int) component_name.len,
            component_name.data
        );
        return 0;
    }

    GgBuffer status = GG_STR("");
    ret = get_lifecycle_state(bus, unit_path, &status);
    if (ret != GG_ERR_OK) {
        return 0;
    }

    if (!is_terminal_state(status)) {
        GG_LOGD(
            "%.*s changed to non-terminal state %.*s; not broadcasting.",
            (int) component_name.len,
            component_name.data,
            (int) status.len,
            status.data
        );
        return 0;
    }

    GG_LOGI(
        "Broadcasting component state change: %.*s -> %.*s",
        (int) component_name.len,
        component_name.data,
        (int) status.len,
        status.data
    );
    respond_state_change(broadcast_handle, component_name, status);
    return 0;
}

void init_health_events(void) {
    while (true) {
        GgError ret = open_bus(&global_bus);
        if (ret == GG_ERR_OK) {
            break;
        }
        GG_LOGE("Failed to open bus.");
        (void) gg_sleep(1);
    }

    do {
        sd_bus_error error = SD_BUS_ERROR_NULL;
        int sd_ret = sd_bus_call_method(
            global_bus,
            DEFAULT_DESTINATION,
            DEFAULT_PATH,
            MANAGER_INTERFACE,
            "Subscribe",
            &error,
            NULL,
            NULL
        );
        GG_CLEANUP(sd_bus_error_free, error);
        if (sd_ret >= 0) {
            break;
        }
        GG_LOGE(
            "Failed to enable bus signals (errno=%d name=%s message=%s).",
            -sd_ret,
            error.name,
            error.message
        );
        (void) gg_sleep(1);
    } while (true);

    sd_event *e = NULL;
    while (true) {
        int sd_ret = sd_event_new(&e);
        if (sd_ret >= 0) {
            break;
        }
        GG_LOGE("Failed to create event loop (errno=%d)", -sd_ret);
        (void) gg_sleep(1);
    }

    int sd_ret = sd_bus_attach_event(global_bus, e, 0);
    if (sd_ret < 0) {
        GG_LOGE("Failed to attach bus event %p", (void *) global_bus);
    }

    // Single global match for every systemd unit's PropertiesChanged. The
    // handler filters to Greengrass component units and terminal states and
    // fans out to the broadcast subscriber. `sender` is restricted to systemd
    // and `path` is NULL (all units), so this one match replaces per-unit
    // matches for the broadcast use case.
    sd_ret = sd_bus_match_signal(
        global_bus,
        &broadcast_match_slot,
        DEFAULT_DESTINATION,
        NULL,
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        global_properties_changed_handler,
        NULL
    );
    if (sd_ret < 0) {
        GG_LOGE(
            "Failed to register global PropertiesChanged match (errno=%d).",
            -sd_ret
        );
    }

    // TODO: replace with setting up a larger epoll
    sd_event_ctx = e;
    ggl_socket_server_ext_fd = sd_event_get_fd(e);
    ggl_socket_server_ext_handler = event_handle_callback;
    GG_LOGD("sd_event_fd %d", ggl_socket_server_ext_fd);
    event_handle_callback();
}

// core-bus functions //

GgError gghealthd_register_lifecycle_subscription(
    GgBuffer component_name, uint32_t handle
) {
    GG_LOGT(
        "Registering watch on %.*s (handle=%" PRIu32 ")",
        (int) component_name.len,
        component_name.data,
        handle
    );

    // find first free slot

    int index = 0;
    for (; index < GGHEALTHD_MAX_SUBSCRIPTIONS; ++index) {
        if (handles[index] == 0) {
            break;
        }
    }
    if (index == GGHEALTHD_MAX_SUBSCRIPTIONS) {
        GG_LOGE("Unable to find open subscription slot.");
        return GG_ERR_NOMEM;
    }

    GG_LOGT("Initializing subscription (index=%d).", index);
    memcpy(component_names[index], component_name.data, component_name.len);
    component_names_len[index] = component_name.len;
    handles[index] = handle;
    GgError ret = register_dbus_signal(index);
    return ret;
}

void gghealthd_unregister_lifecycle_subscription(void *ctx, uint32_t handle) {
    GG_LOGT("Unregistering %" PRIu32, handle);
    (void) ctx;
    for (int index = 0; index < GGHEALTHD_MAX_SUBSCRIPTIONS; ++index) {
        if (handles[index] == handle) {
            GG_LOGT("Found handle (index=%d).", index);
            unregister_dbus_signal(index);
        }
    }
}

GgError gghealthd_register_all_component_state_changes_subscription(
    uint32_t handle
) {
    GG_LOGT(
        "Registering all-component state-change watch (handle=%" PRIu32 ")",
        handle
    );
    if (broadcast_handle != 0) {
        GG_LOGW(
            "Replacing existing all-component state-change subscriber "
            "(old handle=%" PRIu32 ", new handle=%" PRIu32 ").",
            broadcast_handle,
            handle
        );
    }
    broadcast_handle = handle;
    ggl_sub_accept(
        handle,
        gghealthd_unregister_all_component_state_changes_subscription,
        NULL
    );
    GG_LOGD(
        "Accepted all-component state-change subscription (handle=%" PRIu32
        ").",
        handle
    );
    return GG_ERR_OK;
}

void gghealthd_unregister_all_component_state_changes_subscription(
    void *ctx, uint32_t handle
) {
    (void) ctx;
    GG_LOGT(
        "Unregistering all-component state-change watch (handle=%" PRIu32 ")",
        handle
    );
    if (broadcast_handle == handle) {
        broadcast_handle = 0;
        GG_LOGD(
            "Unregistered all-component state-change subscription "
            "(handle=%" PRIu32 ").",
            handle
        );
    }
}
