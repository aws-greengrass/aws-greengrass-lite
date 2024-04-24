#include "ipc_server.hpp"

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data) noexcept {
    return IpcServer::get().lifecycle(moduleHandle, phase, data);
}
