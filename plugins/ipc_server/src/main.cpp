#include "ipc_server.hpp"

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data) noexcept {
    return ipc_server::IpcServer::get().lifecycle(moduleHandle, phase, data);
}
