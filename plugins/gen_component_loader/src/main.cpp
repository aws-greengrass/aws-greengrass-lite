#include "gen_component_loader.hpp"

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data, bool *pHandled) noexcept {
    return GenComponentLoader::get().lifecycle(moduleHandle, phase, data, pHandled);
}
