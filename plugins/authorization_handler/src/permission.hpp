#pragma once

#include <string>

namespace authorization {

    class Permission {
    public:
        std::string principal; // i.e componentName that as access
        std::string operation; // i.e #PublishToTopic
        std::string resource; // i.e #topic
        explicit Permission(std::string principal, std::string operation, std::string resource = "")
            : principal(std::move(principal)), operation(std::move(operation)),
              resource(std::move(resource)){};
    };
} // namespace authorization
