#pragma once

#include "authorization_module.hpp"
#include "authorization_policy.hpp"
#include "permission.hpp"
#include "wildcard_trie.hpp"

#include <logging.hpp>
#include <plugin.hpp>
#include <string>

class AuthorizationModule;

class AuthorizationHandler : public ggapi::Plugin {
    ggapi::Subscription _requestAuthZSub;

private:
    mutable std::shared_mutex _mutex;
    ggapi::Struct _configRoot;

    std::unique_ptr<AuthorizationModule> _authModule;
    std::unique_ptr<AuthorizationPolicyParser> _policyParser;
    std::unordered_map<std::string, std::vector<AuthorizationPolicy>> _componentToAuthZConfig;

    // std::unordered_map<std::string, std::vector<AuthorizationPolicy>> getDefaultPolicies();
    void loadAuthorizationPolicies(
        std::string componentName, std::vector<AuthorizationPolicy> policies, bool isUpdate);
    void validateOperations(std::string componentName, AuthorizationPolicy policy);
    void validatePolicyId(std::vector<AuthorizationPolicy> policies);
    void validatePrincipals(AuthorizationPolicy policy);
    void addPermission(
        std::string destination,
        std::string policyId,
        std::vector<std::string> principals,
        std::vector<std::string> operations,
        std::vector<std::string> resources) noexcept;
    static char asciiToLower(char in);

public:
    AuthorizationHandler() noexcept;
    static constexpr const auto ANY_REGEX = "*";
    bool isAuthorized(
        std::string destination,
        std::string principal,
        std::string operation,
        std::string resource,
        ResourceLookupPolicy resourceLookupPolicy);
    bool isAuthorized(
        std::string destination,
        std::string principal,
        std::string operation,
        std::string resource);

    void onInitialize(ggapi::Struct data) override;
    void onStart(ggapi::Struct data) override;
    bool checkAuthZListenerStart();
    ggapi::Promise checkAuthorized(ggapi::Symbol, const ggapi::Container &callData);
    void checkAuthorizedAsync(const ggapi::Struct &, ggapi::Promise promise);
    static AuthorizationHandler &get() {
        static AuthorizationHandler instance{};
        return instance;
    }
};
