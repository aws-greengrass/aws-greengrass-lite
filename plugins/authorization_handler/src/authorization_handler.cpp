#include "authorization_handler.hpp"

#include <logging.hpp>
#include <plugin.hpp>

static const auto LOG = ggapi::Logger::of("authorization_handler");

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

    try {
        validatePolicyId(policies);
    } catch(const AuthorizationException &e) {
        LOG.atError("load-authorization-config-invalid-policy")
            .log("Component " + componentName + " contains an invalid policy");
        return;
    }

    for(auto policy : policies) {
        try {
            validatePrincipals(policy);
        } catch(const AuthorizationException &e) {
            LOG.atError("load-authorization-config-invalid-principal")
                .log(
                    "Component " + componentName + " contains an invalid principal in policy "
                    + policy.policyId);
            continue;
        }
        try {
            validateOperations(componentName, policy);
        } catch(const AuthorizationException &e) {
            LOG.atError("load-authorization-config-invalid-operation")
                .log(
                    "Component " + componentName + " contains an invalid operation in policy "
                    + policy.policyId);
        }
    }
    if(isUpdate) {
        _authModule->deletePermissionsWithDestination(componentName);
    }
    for(auto policy : policies) {
        try {
            addPermission(
                componentName,
                policy.policyId,
                policy.principals,
                policy.operations,
                policy.resources);
            LOG.atDebug("load-authorization-config")
                .log(
                    "loaded authorization config for " + componentName + " as policy "
                    + policy.policyId);
        } catch(const AuthorizationException &e) {
            LOG.atError("load-authorization-config-add-permission-error")
                .log(
                    "Error while loading policy " + policy.policyId + " for component "
                    + componentName);
        }
    }
    _componentToAuthZConfig.insert({componentName, policies});
    LOG.atDebug("load-authorization-config-success")
        .log("Successfully loaded authorization config for " + componentName);
}

void AuthorizationHandler::validateOperations(
    std::string componentName, AuthorizationPolicy policy) {
    std::vector<std::string> operations = policy.operations;
    if(operations.empty()) {
        throw AuthorizationException(
            "Malformed policy with invalid/empty operations: " + policy.policyId);
    }
    // TODO: check if operations is valid and registered?
}

void AuthorizationHandler::validatePolicyId(std::vector<AuthorizationPolicy> policies) {
    for(auto policy : policies) {
        if(policy.policyId.empty()) {
            throw AuthorizationException("Malformed policy with empty/null policy IDs");
        }
    }
}

void AuthorizationHandler::validatePrincipals(AuthorizationPolicy policy) {
    std::vector<std::string> principals = policy.principals;
    if(principals.empty()) {
        throw AuthorizationException(
            "Malformed policy with invalid/empty principal: " + policy.policyId);
    }
    // TODO: check if principal is a valid EG component
}

void AuthorizationHandler::addPermission(
    std::string destination,
    std::string policyId,
    std::vector<std::string> principals,
    std::vector<std::string> operations,
    std::vector<std::string> resources) noexcept {
    for(auto principal : principals) {
        for(auto operation : operations) {
            if(resources.empty()) {
                Permission permission = Permission(principal, operation);
                _authModule->addPermission(destination, permission);
            } else {
                for(auto resource : resources) {
                    try {
                        Permission permission = Permission(principal, operation, resource);
                        _authModule->addPermission(destination, permission);
                    } catch(std::exception &e) {
                        LOG.atError("load-authorization-config-add-resource-error")
                            .kv("policyId", policyId)
                            .kv("component", principal)
                            .kv("operation", operation)
                            .kv("IPC service", destination)
                            .kv("resource", resource)
                            .log(
                                "Error while adding permission for component " + principal
                                + " to IPC service " + destination);
                    }
                }
            }
        }
    }
}
