#include "fleet_provisioning.hpp"

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data) noexcept {
    return FleetProvisioning::get().lifecycle(moduleHandle, phase, data);
}
