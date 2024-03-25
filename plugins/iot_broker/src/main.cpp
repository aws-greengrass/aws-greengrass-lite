#include "iot_broker.hpp"

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data, bool *pHandled) noexcept {
    return IotBroker::get().lifecycle(moduleHandle, phase, data, pHandled);
}
