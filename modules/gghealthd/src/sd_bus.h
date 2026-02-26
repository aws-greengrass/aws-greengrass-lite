#ifndef GGHEALTHD_SD_BUS_H
#define GGHEALTHD_SD_BUS_H

#include <gg/attr.h>
#include <gg/error.h>
#include <gg/types.h>
#include <ggl/nucleus/constants.h>
#include <systemd/sd-bus.h>

#define SERVICE_PREFIX "ggl."
#define SERVICE_PREFIX_LEN (sizeof(SERVICE_PREFIX) - 1U)
#define SERVICE_SUFFIX ".service"
#define SERVICE_SUFFIX_LEN (sizeof(SERVICE_SUFFIX) - 1U)
#define SERVICE_NAME_MAX_LEN \
    (SERVICE_PREFIX_LEN + GGL_COMPONENT_NAME_MAX_LEN + SERVICE_SUFFIX_LEN)

// destinations
#define DEFAULT_DESTINATION "org.freedesktop.systemd1"

// paths
#define DEFAULT_PATH "/org/freedesktop/systemd1"

// interfaces
#define MANAGER_INTERFACE "org.freedesktop.systemd1.Manager"
#define SERVICE_INTERFACE "org.freedesktop.systemd1.Service"
#define UNIT_INTERFACE "org.freedesktop.systemd1.Unit"

GgError translate_dbus_call_error(int error);

GgError get_unit_path(
    sd_bus *bus,
    const char *qualified_name,
    sd_bus_message **reply,
    const char **unit_path
);

// equivalent to systemd reset-failed <service-name>
NONNULL(2)
void reset_restart_counters(sd_bus *bus, const char *qualified_name);

GgError open_bus(sd_bus **bus);

GgError get_service_name(GgBuffer component_name, GgBuffer *qualified_name);

GgError get_lifecycle_state(
    sd_bus *bus, const char *unit_path, GgBuffer *state
);

NONNULL(2)
GgError restart_component(sd_bus *bus, const char *qualified_name);

void *event_loop_thread(void *ctx);

#endif
