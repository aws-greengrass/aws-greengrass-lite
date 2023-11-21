#pragma once
#include "error_base.hpp"

namespace errors {
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

    class InvalidCallbackError : public Error {
    public:
        explicit InvalidCallbackError(
            const std::string &what = "Invalid Callback specified") noexcept
            : Error("InvalidCallbackError", what) {
        }
    };

    class JsonParseError : public Error {
    public:
        explicit JsonParseError(const std::string &what = "Unable to parse JSON") noexcept
            : Error("JsonParseError", what) {
        }
    };

} // namespace errors
