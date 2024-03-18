#include <shared_device_sdk.hpp>

namespace util {
    Aws::Crt::ApiHandle &getDeviceSdkApiHandle() {
        static Aws::Crt::ApiHandle apiHandle{};
        return apiHandle;
    }
} // namespace util
