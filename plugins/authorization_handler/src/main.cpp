#include "authorization_handler.hpp"

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data) noexcept {
    return authorization::AuthorizationHandler::get().lifecycle(moduleHandle, phase, data);
}
