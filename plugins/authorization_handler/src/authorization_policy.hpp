#pragma once

#include <logging.hpp>
#include <plugin.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class AuthorizationPolicy {

public:
    std::string policyId;
    std::string policyDescription;
    std::vector<std::string> principals{};
    std::vector<std::string> operations{};
    std::vector<std::string> resources{};
    explicit AuthorizationPolicy(
        std::string policyId,
        std::string policyDescription,
        std::vector<std::string> principals,
        std::vector<std::string> operations,
        std::vector<std::string> resources)
        : policyId(std::move(policyId)), policyDescription(std::move(policyDescription)),
          principals(std::move(principals)), operations(std::move(operations)),
          resources(std::move(resources)){};
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
    [[nodiscard]] static std::unordered_map<std::string, std::vector<AuthorizationPolicy>>
    parseAllAuthorizationPolicies(const ggapi::Struct &configRoot);

private:
    static std::unordered_map<std::string, std::vector<AuthorizationPolicy>>
    parseAllPoliciesForComponent(
        const ggapi::Struct &accessControlStruct, const std::string &sourceComponent);

    static std::vector<AuthorizationPolicy> parseAuthorizationPolicyConfig(
        const std::string &componentName,
        const std::unordered_map<std::string, AuthorizationPolicyConfig> &accessControlConfig);
};
