#include "gen_component_loader.hpp"

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data) noexcept {
    return GenComponentLoader::get().lifecycle(moduleHandle, phase, data);
}
