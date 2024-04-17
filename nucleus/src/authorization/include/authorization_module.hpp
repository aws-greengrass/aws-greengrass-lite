#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "authorization_exception.hpp"
#include "authorization_handler.hpp"
#include "permission.hpp"
#include "wildcard_trie.hpp"

namespace authorization {
    class AuthorizationModule {
    public:
        std::unordered_map<std::string, std::unordered_map<std::string, WildcardTrie>>
            resourceAuthZCompleteMap;
        std::unordered_map<
            std::string,
            std::unordered_map<
                std::string,
                std::unordered_map<std::string, std::unordered_set<std::string>>>>
            rawResourceList;

        void addPermission(std::string, Permission permission) noexcept;
        std::unordered_set<std::string> getResources(
            std::string destination, std::string principal, std::string operation) noexcept;
        bool isPresent(
            std::string destination,
            Permission permission,
            ResourceLookupPolicy resourceLookupPolicy) noexcept;

    private:
        void validateResource(std::string resource) noexcept;
    };
} // namespace authorization
