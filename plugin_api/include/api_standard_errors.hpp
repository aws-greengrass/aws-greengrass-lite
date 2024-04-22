#pragma once

#include "api_errors.hpp"

namespace ggapi {

    /**
     * Validation Error - for example IPC parameter validation
     * Naming is based on IPC operations.
     */
    class ValidationError : public GgApiError {
    public:
        inline static const auto KIND = ggapi::Symbol("ggapi::ValidationError");

        explicit ValidationError(const std::string &what = "Validation failed") noexcept
            : GgApiError(KIND, what) {
        }
    };

    /**
     * Access Denied Error - e.g. IPC access is denied
     * Naming is based on IPC operations.
     */
    class AccessDeniedError : public GgApiError {
    public:
        inline static const auto KIND = ggapi::Symbol("ggapi::AccessDenied");

        explicit AccessDeniedError(const std::string &what = "Access is denied") noexcept
            : GgApiError(KIND, what) {
        }
    };

    /**
     * Operation is unsupported (e.g. IPC operation)
     * Naming is based on IPC operations.
     */
    class UnsupportedOperationError : public GgApiError {
    public:
        inline static const auto KIND = ggapi::Symbol("ggapi::UnsupportedOperation");

        explicit UnsupportedOperationError(
            const std::string &what = "Operation not supported") noexcept
            : GgApiError(KIND, what) {
        }
    };

    /**
     * Internal server error (for example IPC operation internal error)
     * Naming is based on IPC operations.
     */
    class InternalServerException : public GgApiError {
    public:
        inline static const auto KIND = ggapi::Symbol("ggapi::InternalServerException");

        explicit InternalServerException(const std::string &what = "Internal error") noexcept
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
