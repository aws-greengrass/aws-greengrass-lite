#pragma once

#include <string>

namespace authorization {
    class Permission {
    public:
        std::string principal; // i.e componentName that as access
        std::string operation; // i.e #PublishToTopic
        std::string resource; // i.e #topic
        explicit Permission(std::string principal, std::string operation)
            : principal(std::move(principal)), operation(std::move(operation)){};
    };

} // namespace authorization
