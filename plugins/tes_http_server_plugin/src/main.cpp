#include "tes_http_server.hpp"

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data, bool *pHandled) noexcept {
    return TesHttpServerPlugin::get().lifecycle(moduleHandle, phase, data, pHandled);
}
