// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "subscriptions.h"
#include "health.h"
#include "sd_bus.h"
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/socket.h>
#include <ggl/utils.h>
#include <inttypes.h>
#include <pthread.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef GGHEALTHD_MAX_SUBSCRIPIONS
#define GGHEALTHD_MAX_SUBSCRIPTIONS 10
#endif

// SoA subscription layout
static sd_bus_slot *slots[GGHEALTHD_MAX_SUBSCRIPTIONS];
static _Atomic(uint32_t) handles[GGHEALTHD_MAX_SUBSCRIPTIONS];
static size_t component_names_len[GGHEALTHD_MAX_SUBSCRIPTIONS];
static uint8_t component_names[GGHEALTHD_MAX_SUBSCRIPTIONS]
                              [COMPONENT_NAME_MAX_LEN];

// only to be used by sd_bus thread.
static sd_bus *bus;

// used to register and unregister subscriptions
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static int event_fd;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static _Atomic(GglError) last_result = GGL_ERR_EXPECTED;
static atomic_int operation_index = -1;

// event_fd must be created before either ggl_listen or dbus threads start
__attribute__((constructor)) static void create_event_fd(void) {
    event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE);
}

static GglBuffer component_name_buf(int index) {
    assert((index >= 0) && (index < GGHEALTHD_MAX_SUBSCRIPTIONS));
    return ggl_buffer_substr(
        GGL_BUF(component_names[index]), 0, component_names_len[index]
    );
}

// Event loop thread functions //

static int properties_changed_handler(
    sd_bus_message *m, void *user_data, sd_bus_error *ret_error
) {
    // index = &slots[index] - &slots[0]
    ptrdiff_t index = ((sd_bus_slot **) user_data) - &slots[0];
    if ((index < 0) || (index >= GGHEALTHD_MAX_SUBSCRIPTIONS)) {
        GGL_LOGE("Bogus index retrieved.");
        sd_bus_error_set_errno(ret_error, -EINVAL);
        return -1;
    }
    if (slots[index] == NULL) {
        GGL_LOGD("Signal received after unref.");
        return -1;
    }

    uint32_t handle = atomic_load(&handles[index]);
    GglBuffer component_name = component_name_buf((int) index);

    const char *unit_path = sd_bus_message_get_path(m);
    if (unit_path == NULL) {
        GGL_LOGD("Message has no path. Skipping signal.");
        return 0;
    }
    GGL_LOGD("Properties changed for %s", unit_path);

    GglBuffer status = GGL_STR("");
    GglError ret = get_lifecycle_state(bus, unit_path, &status);
    if (ret != GGL_ERR_OK) {
        return -1;
    }

    // RUNNING, FINISHED, BROKEN,  terminal states
    if (ggl_buffer_eq(GGL_STR("BROKEN"), status)
        || ggl_buffer_eq(GGL_STR("FINISHED"), status)
        || ggl_buffer_eq(GGL_STR("RUNNING"), status)) {
        GGL_LOGI(
            "%.*s finished their lifecycle (status=%.*s)",
            (int) component_name.len,
            component_name.data,
            (int) status.len,
            status.data
        );
        ggl_respond(
            handle,
            GGL_OBJ_MAP(GGL_MAP(
                { GGL_STR("component_name"), GGL_OBJ_BUF(component_name) },
                { GGL_STR("lifecycle_state"), GGL_OBJ_BUF(status) }
            ))
        );
    } else {
        GGL_LOGD("Signalled for non-terminal state. ");
    }

    return 0;
}

static GglError register_dbus_signal(int index) {
    GGL_LOGD("Event loop thread enabling signal for %d.", index);
    uint8_t qualified_name_bytes[SERVICE_NAME_MAX_LEN + 1];
    GglBuffer qualified_name = GGL_BUF(qualified_name_bytes);
    GglBuffer component_name = component_name_buf(index);
    GglError ret = get_service_name(component_name, &qualified_name);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    sd_bus_message *reply = NULL;
    const char *unit_path = NULL;
    ret = get_unit_path(
        bus, (const char *) qualified_name.data, &reply, &unit_path
    );
    GGL_CLEANUP(sd_bus_message_unrefp, reply);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    sd_bus_slot *slot = NULL;
    int sd_err = sd_bus_match_signal(
        bus,
        &slot,
        NULL,
        unit_path,
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        properties_changed_handler,
        &slots[index]
    );
    if (sd_err < 0) {
        GGL_LOGE(
            "Failed to match signal (unit=%s) (errno=%d)", unit_path, -sd_err
        );
        return translate_dbus_call_error(sd_err);
    }
    slots[index] = slot;
    return GGL_ERR_OK;
}

static GglError unregister_dbus_signal(int index) {
    GGL_LOGD("Event loop thread disabling signal for %d.", index);
    sd_bus_slot_unref(slots[index]);
    atomic_store(&handles[index], 0);
    slots[index] = NULL;
    component_names_len[index] = 0;
    return GGL_ERR_OK;
}

static int event_fd_handler(
    sd_event_source *s, int fd, uint32_t revents, void *userdata
) {
    (void) userdata;
    (void) revents;
    (void) s;
    uint8_t event_bytes[8] = { 0 };
    GglError ret = ggl_read_exact(fd, GGL_BUF(event_bytes));
    pthread_mutex_lock(&mtx);
    if (ret == GGL_ERR_OK) {
        int index = atomic_load(&operation_index);
        assert((index >= 0) && (index < GGHEALTHD_MAX_SUBSCRIPTIONS));
        if (slots[index] == NULL) {
            ret = register_dbus_signal(index);
        } else {
            ret = unregister_dbus_signal(index);
        }
    }
    atomic_store(&last_result, ret);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mtx);
    return 0;
}

void *health_event_loop_thread(void *ctx) {
    (void) ctx;
    while (true) {
        GglError ret = open_bus(&bus);
        if (ret == GGL_ERR_OK) {
            break;
        }
        GGL_LOGE("Failed to open bus.");
        ggl_sleep(1);
    }
    GGL_CLEANUP(sd_bus_unrefp, bus);

    do {
        sd_bus_error error = SD_BUS_ERROR_NULL;
        int sd_ret = sd_bus_call_method(
            bus,
            DEFAULT_DESTINATION,
            DEFAULT_PATH,
            MANAGER_INTERFACE,
            "Subscribe",
            &error,
            NULL,
            NULL
        );
        GGL_CLEANUP(sd_bus_error_free, error);
        if (sd_ret >= 0) {
            break;
        }
        GGL_LOGE(
            "Failed to enable bus signals (errno=%d name=%s message=%s).",
            -sd_ret,
            error.name,
            error.message
        );
        ggl_sleep(1);
    } while (true);

    sd_event *e = NULL;
    while (true) {
        int sd_ret = sd_event_new(&e);
        if (sd_ret >= 0) {
            break;
        }
        GGL_LOGE("Failed to create event loop (errno=%d)", -sd_ret);
        ggl_sleep(1);
    }

    while (true) {
        int sd_ret = sd_event_add_io(
            e, NULL, event_fd, EPOLLIN, event_fd_handler, NULL
        );
        if (sd_ret >= 0) {
            break;
        }
        GGL_LOGE("Failed to add event_fd event (errno=%d)", -sd_ret);
        ggl_sleep(1);
    }

    sd_bus_attach_event(bus, e, 0);

    GGL_LOGD("Started event loop.");
    while (true) {
        int sd_ret = sd_event_loop(e);
        GGL_LOGE("Bailed out of event loop (ret=%d)", sd_ret);
        ggl_sleep(1);
    }

    sd_event_unref(e);
}

// core-bus thread functions //

static GglError signal_event_loop_and_wait(int index) {
    atomic_store(&operation_index, index);
    uint64_t event = 1;
    uint8_t event_bytes[sizeof(event)];
    memcpy(event_bytes, &event, sizeof(event));
    GglError ret = ggl_write_exact(event_fd, GGL_BUF(event_bytes));
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_LOGD("Waiting for sd_bus thread to handle request for %d.", index);
    pthread_mutex_lock(&mtx);
    while ((ret = atomic_exchange(&last_result, GGL_ERR_EXPECTED))
           == GGL_ERR_EXPECTED) {
        pthread_cond_wait(&cond, &mtx);
    }
    pthread_mutex_unlock(&mtx);
    return ret;
}

GglError gghealthd_register_lifecycle_subscription(
    GglBuffer component_name, uint32_t handle
) {
    GGL_LOGT(
        "Registering watch on %.*s (handle=%" PRIu32 ")",
        (int) component_name.len,
        component_name.data,
        handle
    );

    // find first free slot
    int index = 0;
    for (; index < GGHEALTHD_MAX_SUBSCRIPTIONS; ++index) {
        if (atomic_load(&handles[index]) == 0) {
            break;
        }
    }
    if (index == GGHEALTHD_MAX_SUBSCRIPTIONS) {
        GGL_LOGE("Unable to find open subscription slot.");
        return GGL_ERR_NOMEM;
    }

    GGL_LOGT("Initializing subscription (index=%d).", index);

    memcpy(component_names[index], component_name.data, component_name.len);
    component_names_len[index] = component_name.len;
    atomic_store(&handles[index], handle);
    GglError ret = signal_event_loop_and_wait(index);
    return ret;
}

void gghealthd_unregister_lifecycle_subscription(void *ctx, uint32_t handle) {
    GGL_LOGT("Unregistering %" PRIu32, handle);
    (void) ctx;
    for (int index = 0; index < GGHEALTHD_MAX_SUBSCRIPTIONS; ++index) {
        if (atomic_load(&handles[index]) == handle) {
            GGL_LOGT("Found handle (index=%d).", index);
            signal_event_loop_and_wait(index);
        }
    }
}
