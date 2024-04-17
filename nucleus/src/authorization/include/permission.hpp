#pragma once

#include <string>

namespace authorization {
    class Permission {
    public:
        std::string principal;
        std::string operation;
        std::string resource;
        explicit Permission(std::string principal, std::string operation)
            : principal(std::move(principal)), operation(std::move(operation)){};
    };

} // namespace authorization
