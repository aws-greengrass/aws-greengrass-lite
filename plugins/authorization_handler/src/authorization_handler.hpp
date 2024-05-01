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
    bool checkAuthZListenerStart();
    ggapi::Promise checkAuthorized(ggapi::Symbol, const ggapi::Container &callData);
    void checkAuthorizedAsync(const ggapi::Struct &, ggapi::Promise promise);
    bool isAuthorized(
        std::string destination,
        std::string principal,
        std::string operation,
        std::string resource,
        ResourceLookupPolicy resourceLookupPolicy);
    void loadAuthorizationPolicies(
        const std::string &componentName,
        const std::vector<AuthorizationPolicy> &policies,
        bool isUpdate);
    void validateOperations(
        const std::string &componentName, const AuthorizationPolicy &policy) const;
    void validatePolicyId(const std::vector<AuthorizationPolicy> &policies) const;
    void validatePrincipals(const AuthorizationPolicy &policy) const;
    void addPermission(
        const std::string &destination,
        const std::string &policyId,
        const std::vector<std::string> &principals,
        const std::vector<std::string> &operations,
        const std::vector<std::string> &resources) noexcept;

public:
    AuthorizationHandler() noexcept;
    void onInitialize(ggapi::Struct data) override;
    void onStart(ggapi::Struct data) override;
    static AuthorizationHandler &get() {
        static AuthorizationHandler instance{};
        return instance;
    }
};
