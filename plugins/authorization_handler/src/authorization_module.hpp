#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "authorization_handler.hpp"
#include "permission.hpp"
#include "wildcard_trie.hpp"
#include <logging.hpp>
#include <plugin.hpp>

class AuthorizationModule {
public:
    void addPermission(std::string destination, Permission permission);
    std::vector<std::string> getResources(
        std::string destination, std::string principal, std::string operation);
    bool isPresent(
        std::string destination, Permission permission, ResourceLookupPolicy lookupPolicy);
    bool isPresent(std::string destination, Permission permission);
    void deletePermissionsWithDestination(std::string destination);

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
    void validateResource(std::string resource);
    std::vector<std::string> addResourceInternal(
        std::vector<std::string> out,
        std::string destination,
        std::string principal,
        std::string operation);
    bool isSpecialChar(char actualChar);
};

class AuthorizationException : public ggapi::GgApiError {
public:
    AuthorizationException(const std::string &msg)
        : ggapi::GgApiError("AuthorizationException", msg) {
    }
};
