#include "example_mqtt_sender.hpp"

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data, bool *pHandled) noexcept {
    return MqttSender::get().lifecycle(moduleHandle, phase, data, pHandled);
}
