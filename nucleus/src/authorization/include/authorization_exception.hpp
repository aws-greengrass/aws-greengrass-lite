#pragma once

#include "errors/errors.hpp"

namespace authorization {
    class AuthorizationException : public errors::Error {
    public:
        explicit AuthorizationException(const std::string &msg) noexcept
            : errors::Error("AuthorizationException", msg) {
        }
    };
} // namespace authorization
