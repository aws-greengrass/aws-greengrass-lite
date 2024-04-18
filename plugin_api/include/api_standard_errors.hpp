#pragma once

#include "api_errors.hpp"

namespace ggapi {

    /**
     * Validation Error - e.g. IPC parameter validation
     */
    class ValidationError : public GgApiError {
    public:
        inline static const auto KIND = ggapi::Symbol("ValidationError");

        explicit ValidationError(const std::string &what = "ValidationError") noexcept
            : GgApiError(KIND, what) {
        }
    };

    /**
     * Access Denied Error - e.g. IPC access is denied
     */
    class AccessDeniedError : public GgApiError {
    public:
        inline static const auto KIND = ggapi::Symbol("AccessDenied");

        explicit AccessDeniedError(const std::string &what = "AccessDenied") noexcept
            : GgApiError(KIND, what) {
        }
    };

    /**
     * Operation is unsupported (typically used for IPC)
     */
    class UnsupportedOperationError : public GgApiError {
    public:
        inline static const auto KIND = ggapi::Symbol("UnsupportedOperation");

        explicit UnsupportedOperationError(
            const std::string &what = "UnsupportedOperation") noexcept
            : GgApiError(KIND, what) {
        }
    };

    /**
     * Internal server error (typically used for IPC)
     */
    class InternalServerException : public GgApiError {
    public:
        inline static const auto KIND = ggapi::Symbol("InternalServerException");

        explicit InternalServerException(
            const std::string &what = "InternalServerException") noexcept
            : GgApiError(KIND, what) {
        }
    };

    /**
     * Unhandled Lifecycle event
     */
    class UnhandledLifecycleEvent : public GgApiError {
        inline static const auto KIND = ggapi::Symbol("UnhandledLifecycleEvent");

        explicit UnhandledLifecycleEvent(
            const std::string &what = "UnhandledLifecycleEvent") noexcept
            : GgApiError(KIND, what) {
        }
    };
} // namespace ggapi
