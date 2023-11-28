#include "ipc_plugin.hpp"

extern "C" bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t data) noexcept {
    return IpcPlugin::get().lifecycle(moduleHandle, phase, data);
}
