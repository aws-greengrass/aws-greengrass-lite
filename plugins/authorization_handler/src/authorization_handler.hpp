#pragma once

#include "authorization_module.hpp"
#include "authorization_policy.hpp"
#include "permission.hpp"

#include <logging.hpp>
#include <plugin.hpp>
#include <string>

namespace authorization {
    class AuthorizationHandler : public ggapi::Plugin {
    private:
        mutable std::shared_mutex _mutex;
        ggapi::Struct _configRoot;

        std::unique_ptr<AuthorizationModule> _authModule;
        std::unique_ptr<AuthorizationPolicyParser> _policyParser;

        // std::unordered_map<std::string, std::vector<AuthorizationPolicy>> getDefaultPolicies();
        void loadAuthorizationPolicies(
            std::string componentName, std::vector<AuthorizationPolicy> policies, bool isUpdate);

    public:
        AuthorizationHandler() noexcept;
        static constexpr const auto ANY_REGEX = "*";
        bool isAuthorized(std::string destination, Permission permission);

        void onInitialize(ggapi::Struct data) override;
        void onStart(ggapi::Struct data) override;

        static AuthorizationHandler &get() {
            static AuthorizationHandler instance{};
            return instance;
        }
    };

    enum class ResourceLookupPolicy { STANDARD, MQTT_STYLE };
} // namespace authorization
