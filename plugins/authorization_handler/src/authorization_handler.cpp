#include "authorization_handler.hpp"

namespace authorization {
    AuthorizationHandler::AuthorizationHandler() noexcept {
        _authModule = std::make_unique<AuthorizationModule>();
        _policyParser = std::make_unique<AuthorizationPolicyParser>();
    }

    bool AuthorizationHandler::isAuthorized(std::string destination, Permission permission) {
        // TODO: check if destination service is registered with specific operation
        // TODO: if reg'd, check if permission exists in authZ module
        return true;
    }

    void AuthorizationHandler::onInitialize(ggapi::Struct data) {
        data.put(NAME, "aws.greengrass.authorization_handler");
        std::unique_lock guard{_mutex};
        _configRoot = data.getValue<ggapi::Struct>({"configRoot"});
    }

    void AuthorizationHandler::onStart(ggapi::Struct data) {
        std::unique_lock guard{_mutex};

        auto configRoot = _configRoot;

        std::unordered_map<std::string, std::vector<AuthorizationPolicy>> componentNameToPolicies =
            _policyParser->parseAllAuthorizationPolicies(configRoot);
        std::unordered_map<std::string, std::vector<AuthorizationPolicy>> defaultPoliciesMap = {};
        componentNameToPolicies.insert(defaultPoliciesMap.begin(), defaultPoliciesMap.end());

        for(auto acl : componentNameToPolicies) {
            loadAuthorizationPolicies(acl.first, acl.second, false);
        }

        // TODO: Add watcher on config changes to the service accessControl block
    }

    void AuthorizationHandler::loadAuthorizationPolicies(
        std::string componentName, std::vector<AuthorizationPolicy> policies, bool isUpdate) {
        if(policies.empty()) {
            return;
        }

        // TODO: validation of policy (policyId, registration?, principals, operations)
        // TODO: if updating, remove policy first -> re-add
        // TODO: add permissions to config and authmodule
    }
    /*
        std::unordered_map<std::string, std::vector<AuthorizationPolicy>> getDefaultPolicies() {
            std::unordered_map<std::string, std::vector<AuthorizationPolicy>> allDefaultPolicies =
       {};

            // TODO: Add any default policies to be loaded. i.e TES

            return allDefaultPolicies;
        }
    */
} // namespace authorization
