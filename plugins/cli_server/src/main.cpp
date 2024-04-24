#include "cli_server.hpp"

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data) noexcept {
    return CliServer::get().lifecycle(moduleHandle, phase, data);
}
