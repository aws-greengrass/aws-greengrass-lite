#include "provision_plugin.hpp"

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data, bool *pHandled) noexcept {
    return ProvisionPlugin::get().lifecycle(moduleHandle, phase, data, pHandled);
}
