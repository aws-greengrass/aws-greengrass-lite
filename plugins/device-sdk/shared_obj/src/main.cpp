#include <iostream>
#include <shared_device_sdk.hpp>

namespace util {
    Aws::Crt::ApiHandle &getDeviceSdkApiHandle() {
        static auto &handle = []() -> Aws::Crt::ApiHandle & {
            static Aws::Crt::ApiHandle apiHandle{};
            try {
                apiHandle.InitializeLogging(Aws::Crt::LogLevel::Info, stderr);
            } catch(const std::exception &e) {
                std::cerr << "[device-sdk] probably did not initialize the logging: " << e.what()
                          << std::endl;
            }
            return apiHandle;
        }();
        return handle;
    }
} // namespace util
