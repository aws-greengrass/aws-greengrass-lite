#pragma once
#include "error_base.hpp"

namespace errors {

    class InvalidContextError : public Error {
    public:
        explicit InvalidContextError(
            const std::string &what =
                "Environment Context cannot be determined - possibly released") noexcept
            : Error("InvalidContextError", what) {
        }
    };

    class NullHandleError : public Error {
    public:
        explicit NullHandleError(const std::string &what = "Null Handle specified") noexcept
            : Error("NullHandleError", what) {
        }
    };

    class InvalidHandleError : public Error {
    public:
        explicit InvalidHandleError(const std::string &what = "Invalid Handle specified") noexcept
            : Error("InvalidHandleError", what) {
        }
    };

    class NullSymbolError : public Error {
    public:
        explicit NullSymbolError(const std::string &what = "Null Symbol specified") noexcept
            : Error("NullSymbolError", what) {
        }
    };

    class InvalidSymbolError : public Error {
    public:
        explicit InvalidSymbolError(const std::string &what = "Invalid Symbol specified") noexcept
            : Error("InvalidSymbolError", what) {
        }
    };

    class InvalidTaskError : public Error {
    public:
        explicit InvalidTaskError(const std::string &what = "Invalid Task specified") noexcept
            : Error("InvalidTaskError", what) {
        }
    };

    class InvalidContainerError : public Error {
    public:
        explicit InvalidContainerError(
            const std::string &what = "Container reference is expected") noexcept
            : Error("InvalidContainerError", what) {
        }
    };

    class InvalidListError : public Error {
    public:
        explicit InvalidListError(const std::string &what = "List container is expected") noexcept
            : Error("InvalidListError", what) {
        }
    };

    class InvalidStructError : public Error {
    public:
        explicit InvalidStructError(
            const std::string &what = "Structure container is expected") noexcept
            : Error("InvalidStructError", what) {
        }
    };

    class InvalidConfigTopicsError : public Error {
    public:
        explicit InvalidConfigTopicsError(
            const std::string &what = "Config topics reference is expected") noexcept
            : Error("InvalidConfigTopicsError", what) {
        }
    };

    class InvalidBufferError : public Error {
    public:
        explicit InvalidBufferError(
            const std::string &what = "Buffer container is expected") noexcept
            : Error("InvalidBufferError", what) {
        }
    };

    class InvalidModuleError : public Error {
    public:
        explicit InvalidModuleError(
            const std::string &what = "Module reference is expected") noexcept
            : Error("InvalidModuleError", what) {
        }
    };

    class InvalidScopeError : public Error {
    public:
        explicit InvalidScopeError(const std::string &what = "Scope reference is expected") noexcept
            : Error("InvalidScopeError", what) {
        }
    };

    class InvalidSubscriberError : public Error {
    public:
        explicit InvalidSubscriberError(
            const std::string &what = "Invalid Subscriber specified") noexcept
            : Error("InvalidSubscriberError", what) {
        }
    };

    class CallbackError : public Error {
    public:
        explicit CallbackError(const std::string &what = "Callback error") noexcept
            : Error("CallbackError", what) {
        }
    };

    class JsonParseError : public Error {
    public:
        explicit JsonParseError(const std::string &what = "Unable to parse JSON") noexcept
            : Error("JsonParseError", what) {
        }
    };

    class CommandLineArgumentError : public Error {
    public:
        explicit CommandLineArgumentError(const std::string &what) noexcept
            : Error("CommandLineArgumentError", what) {
        }
    };

    class BootError : public Error {
    public:
        explicit BootError(const std::string &what) noexcept : Error("BootError", what) {
        }
    };

    class ModuleError : public Error {
    public:
        explicit ModuleError(const std::string &what) noexcept : Error("ModuleError", what) {
        }
    };

} // namespace errors
