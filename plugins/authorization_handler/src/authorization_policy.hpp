#pragma once

#include <logging.hpp>
#include <plugin.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace lifecycle {
    class Kernel;
}

namespace authorization {
    class AuthorizationPolicy {
    private:
        std::string _policyId;
        std::string _policyDescription;
        std::vector<std::string> _principals{};
        std::vector<std::string> _operations{};
        std::vector<std::string> _resources{};

    public:
        explicit AuthorizationPolicy(
            std::string policyId,
            std::string policyDescription,
            std::vector<std::string> principals,
            std::vector<std::string> operations,
            std::vector<std::string> resources)
            : _policyId(std::move(policyId)), _policyDescription(std::move(policyDescription)),
              _principals(std::move(principals)), _operations(std::move(operations)),
              _resources(std::move(resources)){};

        std::vector<std::string> getOperations() {
            return _operations;
        }
    };

    struct AuthorizationPolicyConfig : ggapi::Serializable {
    public:
        std::vector<std::string> operations;
        std::string policyDescription;
        std::vector<std::string> resources;

        void visit(ggapi::Archive &archive) override {
            archive.setIgnoreCase();
            archive("operations", operations);
            archive("policyDescription", policyDescription);
            archive("resources", resources);
        }
    };

    class AuthorizationPolicyParser {
    public:
        explicit AuthorizationPolicyParser();
        [[nodiscard]] std::unordered_map<std::string, std::vector<AuthorizationPolicy>>
        parseAllAuthorizationPolicies(ggapi::Struct configRoot);

    private:
        std::unordered_map<std::string, std::vector<AuthorizationPolicy>>
        parseAllPoliciesForComponent(
            ggapi::Struct accessControlStruct, std::string sourceComponent);

        std::vector<AuthorizationPolicy> parseAuthorizationPolicyConfig(
            std::string componentName,
            std::unordered_map<std::string, AuthorizationPolicyConfig> accessControlConfig);
    };
} // namespace authorization
