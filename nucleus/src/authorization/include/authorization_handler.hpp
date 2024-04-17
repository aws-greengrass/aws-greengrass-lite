#pragma once

namespace authorization {
    class AuthorizationHandler {
    public:
        static constexpr const auto ANY_REGEX = "*";
        explicit AuthorizationHandler(){};
    };

    enum class ResourceLookupPolicy { STANDARD, MQTT_STYLE };
} // namespace authorization
