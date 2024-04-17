#pragma once

#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace authorization {
    class AuthorizationPolicy {
    private:
        std::string _policyId;
        std::string _policyDescription;
        std::unordered_set<std::string> _principals{};
        std::unordered_set<std::string> _operations{};
        std::unordered_set<std::string> _resources{};

    public:
        explicit AuthorizationPolicy(){};
    };

    class AuthorizationPolicyConfig {
    public:
        std::string policyDescription;
        std::unordered_set<std::string> operations{};
        std::unordered_set<std::string> resources{};

        explicit AuthorizationPolicyConfig(
            std::string policyDescription,
            std::unordered_set<std::string> operations,
            std::unordered_set<std::string> resources)
            : policyDescription(std::move(policyDescription)), operations(std::move(operations)),
              resources(std::move(resources)){};
    };

    class AuthorizationPolicyParser {
    public:
        [[nodiscard]] std::unordered_map<std::string, std::list<AuthorizationPolicy>>
        parseAllAuthorizationPolicies();

    private:
        std::unordered_map<std::string, std::list<AuthorizationPolicy>>
        parseAllPoliciesForComponent(std::string sourceComponent);

        std::list<AuthorizationPolicy> parseAuthorizationPolicyConfig(
            std::string componentName,
            std::unordered_map<std::string, AuthorizationPolicyConfig> accessControlConfig);
    };
} // namespace authorization
