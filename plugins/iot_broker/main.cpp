
#include "src/iot_broker.hpp"

// Initializes global CRT API
// TODO: What happens when multiple plugins use the CRT?
static Aws::Crt::ApiHandle apiHandle{};

extern "C" [[maybe_unused]] bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t dataHandle) noexcept {
    static std::once_flag loggingInitialized;
    std::call_once(loggingInitialized, []() {
        apiHandle.InitializeLogging(Aws::Crt::LogLevel::Info, stderr);
    });
    return IotBroker::get().lifecycle(moduleHandle, phase, dataHandle);
}

static std::ostream &operator<<(std::ostream &os, Aws::Crt::ByteCursor bc) {
    for(int byte : std::basic_string_view<uint8_t>(bc.ptr, bc.len)) {
        if(isprint(byte)) {
            os << static_cast<char>(byte);
        } else {
            os << '\\' << byte;
        }
    }
    return os;
}
