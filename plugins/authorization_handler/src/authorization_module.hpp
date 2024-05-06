#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "permission.hpp"
#include "wildcard_trie.hpp"
#include <logging.hpp>
#include <plugin.hpp>

class AuthorizationModule {
public:
    static constexpr const auto ANY_REGEX = "*";
    void addPermission(const std::string &destination, const Permission &permission);
    std::vector<std::string> getResources(
        const std::string &destination, const std::string &principal, const std::string &operation);
    bool isPresent(
        const std::string &destination,
        const Permission &permission,
        ResourceLookupPolicy lookupPolicy);
    void deletePermissionsWithDestination(const std::string &destination);

private:
    std::unordered_map<
        std::string,
        std::unordered_map<
            std::string,
            std::unordered_map<std::string, std::shared_ptr<WildcardTrie>>>>
        _resourceAuthZCompleteMap;
    std::unordered_map<
        std::string,
        std::unordered_map<std::string, std::unordered_map<std::string, std::vector<std::string>>>>
        _rawResourceList;
    static void validateResource(const std::string &resource);
    std::vector<std::string> addResourceInternal(
        std::vector<std::string> out,
        const std::string &destination,
        const std::string &principal,
        const std::string &operation);
};

class AuthorizationException : public ggapi::GgApiError {
public:
    explicit AuthorizationException(const std::string &msg)
        : ggapi::GgApiError("AuthorizationException", msg) {
    }
};
