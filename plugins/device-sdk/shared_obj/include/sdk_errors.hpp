#pragma once

#include <api_errors.hpp>

#include "shared_device_sdk.hpp"

namespace util {
    class AwsCrtError : public ggapi::GgApiError {
    public:
        inline static const auto KIND = ggapi::Symbol("ggapi::AwsCrtError");

        explicit AwsCrtError(const std::string &what) noexcept : ggapi::GgApiError(KIND, what) {
        }

        explicit AwsCrtError(int errorCode) : AwsCrtError(util::getAwsCrtErrorString(errorCode)) {
        }
    };
} // namespace util
