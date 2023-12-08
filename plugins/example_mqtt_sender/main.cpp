#include "example_mqtt_sender.hpp"

extern "C" [[maybe_unused]] bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t data) noexcept {
    return ExampleMqttSender::get().lifecycle(moduleHandle, phase, data);
}
