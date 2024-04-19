#include <iostream>
#include <shared_device_sdk.hpp>

namespace util {
    /**
     * Any and every library that uses device SDK must call this function. This will ensure that
     * the DeviceSDK will be initialized.
     *
     * @return handle that may be used by other functions.
     */
    // NOLINTNEXTLINE(*-avoid-non-const-global-variables) ApiHandle is a singleton
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

    /**
     * Retrieve error string that equates to error code. While in practice the returned
     * string is a reference to a constant string, assume this could change in the future.
     * The string should be used almost immediately and should assume the string could change
     * if another exception has been thrown.
     *
     * @param errorCode AWS SDK error code
     * @return counted reference to string
     */
    std::string_view getAwsCrtErrorString(int errorCode) noexcept {
        return {aws_error_str(errorCode)};
    }

} // namespace util
