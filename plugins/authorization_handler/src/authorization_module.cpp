#include "authorization_module.hpp"

#include "wildcard_trie.hpp"

void AuthorizationModule::addPermission(std::string destination, Permission permission) {
    if(destination.empty() || permission.principal.empty() || permission.operation.empty()) {
        throw AuthorizationException("Invalid arguments");
    }
    std::string resource = permission.resource;
    validateResource(resource);

    auto &destMap = _resourceAuthZCompleteMap[destination];
    auto &principalMap = destMap[permission.principal];
    auto &resourceTrie = principalMap[permission.operation];
    if(!resourceTrie) {
        resourceTrie = std::make_shared<WildcardTrie>();
    }
    resourceTrie->add(resource);

    auto &destMapRaw = _rawResourceList[destination];
    auto &principalMapRaw = destMapRaw[permission.principal];
    auto &resourceVectorRaw = principalMapRaw[permission.operation];
    resourceVectorRaw.push_back(resource);
}

void AuthorizationModule::validateResource(std::string resource) {
    if(resource.empty()) {
        throw AuthorizationException("Resource cannot be empty");
    }
    int length = resource.size();
    for(int i = 0; i < length; i++) {
        auto currentChar = resource[i];
        if(currentChar == WildcardTrie::escapeChar && i + 1 < length && resource[i + 1] == '{') {
            auto actualChar = WildcardTrie::getActualChar(resource.substr(i));
            if(actualChar == WildcardTrie::nullChar || !isSpecialChar(actualChar)) {
                throw AuthorizationException(
                    "Resource contains an invalid escape sequence. You can use *, $, or ?");
            }
            // skip next 3 characters as they are accounted for in escape sequence
            i = i + 3;
        }
        if(currentChar == WildcardTrie::singleCharWildcard) {
            throw AuthorizationException(
                "Resource contains invalid character: '?'. Use an escape sequence: ${?}. The "
                "'?' character isn't supported as a wildcard");
        }
    }
}

bool AuthorizationModule::isSpecialChar(char actualChar) {
    return (
        actualChar == WildcardTrie::wildcardChar || actualChar == WildcardTrie::escapeChar
        || actualChar == WildcardTrie::singleCharWildcard);
}

void AuthorizationModule::deletePermissionsWithDestination(std::string destination) {
    _resourceAuthZCompleteMap.erase(destination);
    _rawResourceList.erase(destination);
}

bool AuthorizationModule::isPresent(
    std::string destination, Permission permission, ResourceLookupPolicy resourceLookupPolicy) {
    if(destination.empty() || permission.principal.empty() || permission.operation.empty()) {
        throw AuthorizationException("Invalid arguments");
    }
    std::string resource = permission.resource;
    if(resource.empty()) {
        throw AuthorizationException("Resource cannot be empty");
    }
    if(auto it = _resourceAuthZCompleteMap.find(destination);
       it != _resourceAuthZCompleteMap.end()) {
        auto destMap = it->second;
        if(auto desIt = destMap.find(permission.principal); desIt != destMap.end()) {
            auto principalMap = desIt->second;
            if(auto principalIt = principalMap.find(permission.operation);
               principalIt != principalMap.end()) {
                return principalIt->second->matches(resource, resourceLookupPolicy);
            }
        }
    }
    return false;
}

bool AuthorizationModule::isPresent(std::string destination, Permission permission) {
    return isPresent(destination, permission, ResourceLookupPolicy::STANDARD);
}

std::vector<std::string> AuthorizationModule::getResources(
    std::string destination, std::string principal, std::string operation) {
    if(destination.empty() || principal.empty() || operation.empty()
       || !principal.compare(AuthorizationHandler::ANY_REGEX)
       || !operation.compare(AuthorizationHandler::ANY_REGEX)) {
        throw AuthorizationException("Invalid arguments");
    }

    std::vector<std::string> out;
    out = addResourceInternal(out, destination, principal, operation);
    out = addResourceInternal(out, destination, AuthorizationHandler::ANY_REGEX, operation);
    out = addResourceInternal(out, destination, principal, AuthorizationHandler::ANY_REGEX);

    return out;
}

std::vector<std::string> AuthorizationModule::addResourceInternal(
    std::vector<std::string> out,
    std::string destination,
    std::string principal,
    std::string operation) {
    if(auto it = _rawResourceList.find(destination); it != _rawResourceList.end()) {
        auto destMap = it->second;
        if(auto desIt = destMap.find(principal); desIt != destMap.end()) {
            auto principalMap = desIt->second;
            if(auto principalIt = principalMap.find(operation); principalIt != principalMap.end()) {
                auto vec = principalIt->second;
                for(auto e : vec) {
                    out.push_back(e);
                }
            }
        }
    }

    return out;
}
