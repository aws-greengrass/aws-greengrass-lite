#pragma once

#include "api_errors.hpp"

/**
 * IPC Modelled Errors - note, some errors require the error string to be provided to give
 * more context.
 */
namespace ggapi::ipc {

    //
    // All IPC Modeled errors begin: "IPC:Modeled::" and are assumed to follow the greengrass
    // modeled IPC syntax.
    //

    class IpcError : public GgApiError {
        inline static const auto SERVICE = ggapi::Symbol("aws.greengrass#GreengrassCoreIPC");

    public:
        using GgApiError::GgApiError;
    };

    class ConflictError : public IpcError {
        inline static const auto KIND =
            ggapi::Symbol("IPC::Modeled::aws.greengrass#InvalidTokenError");

    public:
        explicit ConflictError(const std::string &err) noexcept : IpcError(KIND, err) {
        }
    };

    class InvalidTokenError : public IpcError {
        inline static const auto KIND =
            ggapi::Symbol("IPC::Modeled::aws.greengrass#InvalidTokenError");

    public:
        explicit InvalidTokenError(const std::string &err) noexcept : IpcError(KIND, err) {
        }
    };

    class UnauthorizedError : public IpcError {
        inline static const auto KIND =
            ggapi::Symbol("IPC::Modeled::aws.greengrass#UnauthorizedError");

    public:
        explicit UnauthorizedError(const std::string &err) noexcept : IpcError(KIND, err) {
        }
    };

    class InvalidArgumentsError : public IpcError {
        inline static const auto KIND =
            ggapi::Symbol("IPC::Modeled::aws.greengrass#InvalidArgumentsError");

    public:
        explicit InvalidArgumentsError(
            const std::string &err = "One or more arguments are invalid") noexcept
            : IpcError(KIND, err) {
        }
    };

    class ComponentNotFoundError : public IpcError {
        inline static const auto KIND =
            ggapi::Symbol("IPC::Modeled::aws.greengrass#ComponentNotFoundError");

    public:
        explicit ComponentNotFoundError(const std::string &err) noexcept : IpcError(KIND, err) {
        }
    };

    class InvalidCredentialError : public IpcError {
        inline static const auto KIND =
            ggapi::Symbol("IPC::Modeled::aws.greengrass#InvalidCredentialError");

    public:
        explicit InvalidCredentialError(const std::string &err) noexcept : IpcError(KIND, err) {
        }
    };

    class ServiceError : public IpcError {
        inline static const auto KIND = ggapi::Symbol("IPC::Modeled::aws.greengrass#ServiceError");

    public:
        explicit ServiceError(const std::string &err = "Required service failed") noexcept
            : IpcError(KIND, err) {
        }
    };

    class FailedUpdateConditionCheckError : public IpcError {
        inline static const auto KIND =
            ggapi::Symbol("IPC::Modeled::aws.greengrass#FailedUpdateConditionCheckError");

    public:
        explicit FailedUpdateConditionCheckError(const std::string &err) noexcept
            : IpcError(KIND, err) {
        }
    };

    class InvalidRecipeDirectoryPathError : public IpcError {
        inline static const auto KIND =
            ggapi::Symbol("IPC::Modeled::aws.greengrass#InvalidRecipeDirectoryPathError");

    public:
        explicit InvalidRecipeDirectoryPathError(
            const std::string &err = "Recipe directory is invalid") noexcept
            : IpcError(KIND, err) {
        }
    };

    class InvalidClientDeviceAuthTokenError : public IpcError {
        inline static const auto KIND =
            ggapi::Symbol("IPC::Modeled::aws.greengrass#InvalidClientDeviceAuthTokenError");

    public:
        explicit InvalidClientDeviceAuthTokenError(const std::string &err) noexcept
            : IpcError(KIND, err) {
        }
    };

    class InvalidArtifactsDirectoryPathError : public IpcError {
        inline static const auto KIND =
            ggapi::Symbol("IPC::Modeled::aws.greengrass#InvalidArtifactsDirectoryPathError");

    public:
        explicit InvalidArtifactsDirectoryPathError(
            const std::string &err = "Artifacts directory is invalid") noexcept
            : IpcError(KIND, err) {
        }
    };

    class ResourceNotFoundError : public IpcError {
        inline static const auto KIND =
            ggapi::Symbol("IPC::Modeled::aws.greengrass#ResourceNotFoundError");

    public:
        explicit ResourceNotFoundError(
            const std::string &err = "Required resource is not found") noexcept
            : IpcError(KIND, err) {
        }
    };
} // namespace ggapi::ipc
