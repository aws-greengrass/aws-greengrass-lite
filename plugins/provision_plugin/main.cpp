#include "provision_plugin.hpp"

extern "C" bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t dataHandle) noexcept {
    return ProvisionPlugin::get().lifecycle(moduleHandle, phase, dataHandle);
}
