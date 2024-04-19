#pragma once

#include <api_errors.hpp>

#include "shared_device_sdk.hpp"

namespace util {
    /**
     * Wrap an AWS CRT API Error as a runtime exception where the error kind can persist
     * between Nucleus and Plugin API
     */
    class AwsCrtError : public ggapi::GgApiError {
    public:
        inline static const auto KIND = ggapi::Symbol("ggapi::AwsCrtError");

        explicit AwsCrtError(const std::string &what) noexcept : ggapi::GgApiError(KIND, what) {
        }

        explicit AwsCrtError(int errorCode) : AwsCrtError(util::getAwsCrtErrorString(errorCode)) {
        }
    };
} // namespace util
