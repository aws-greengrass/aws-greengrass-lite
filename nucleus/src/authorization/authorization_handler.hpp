#pragma once

#include "permission.hpp"

#include <string>

namespace authorization {
    class AuthorizationHandler {
    public:
        static constexpr const auto ANY_REGEX = "*";
        explicit AuthorizationHandler(){};
        bool isAuthorized(std::string destination, Permission permission);
    };

    enum class ResourceLookupPolicy { STANDARD, MQTT_STYLE };
} // namespace authorization
