#pragma once
#include <api_errors.hpp>
#include <shared_device_sdk.hpp>

namespace util {
    class AwsSdkError : public ggapi::GgApiError {

        [[nodiscard]] static std::string formError(int errorCode, const std::string &prefix) {
            if(prefix.empty()) {
                return formError(errorCode);
            } else {
                return prefix + ": " + formError(errorCode);
            }
        }

        [[nodiscard]] static std::string formError(int errorCode) {
            return util::getAwsCrtErrorString(errorCode);
        }

    public:
        static inline const KindType KIND{"DeviceSdkError"};

        explicit AwsSdkError(int errorCode, const std::string &prefix = "")
            : ggapi::GgApiError(KIND, formError(errorCode, prefix)) {
        }
        explicit AwsSdkError(const std::string &prefix = "")
            : AwsSdkError(aws_last_error(), prefix) {
        }
    };
} // namespace util
