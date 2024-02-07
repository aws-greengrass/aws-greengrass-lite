#include "native_plugin.hpp"

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle,
    ggapiSymbol phase,
    ggapiObjHandle data,
    bool *pWasHandled) noexcept {
    return NativePlugin::get().lifecycle(moduleHandle, phase, data, pWasHandled);
}
