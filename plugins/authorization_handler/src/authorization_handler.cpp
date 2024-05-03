#include "authorization_handler.hpp"

#include <logging.hpp>
#include <plugin.hpp>

static const auto LOG = ggapi::Logger::of("authorization_handler");

const AuthorizationHandler::Keys AuthorizationHandler::keys{};

ggapi::Promise AuthorizationHandler::checkAuthorized(
    ggapi::Symbol, const ggapi::Container &callData) {
    return ggapi::Promise::create().async(
        &AuthorizationHandler::checkAuthorizedAsync, this, ggapi::Struct(callData));
}

void AuthorizationHandler::checkAuthorizedAsync(
    const ggapi::Struct &callData, ggapi::Promise promise) {
    promise.fulfill([&]() {
        // Invoke the isAuthorized function using the lpc call meta data received in the callData
        // struct. Lpc operations can optionally check if their caller is authorized to make such a
        // request using this meta data.
        auto destination = callData.get<std::string>(keys.destination);
        auto principal = callData.get<std::string>(keys.principal);
        auto operation = callData.get<std::string>(keys.operation);
        auto resource = callData.get<std::string>(keys.resource);
        auto resourceType = callData.get<std::string>(keys.resourceType);

        bool success;
        try {
            auto resourceTypeSelection = ResourceLookupPolicy::UNKNOWN;
            if(resourceType == keys.MQTT.toString()) {
                resourceTypeSelection = ResourceLookupPolicy::MQTT_STYLE;
            } else if(resourceType.empty()) {
                resourceTypeSelection = ResourceLookupPolicy::STANDARD;
            } else {
                throw ggapi::GgApiError(
                    "ggapi::AuthorizationException",
                    "Unknown resource type exception: " + resourceType);
            }
            Permission permission = Permission(principal, operation, resource);
            success = isAuthorized(destination, permission, resourceTypeSelection);
        } catch(const AuthorizationException &e) {
            throw ggapi::GgApiError("ggapi::AuthorizationException", e.what());
        }

        LOG.atDebug().event("Check Authorized Status").log("Completed checking if authorized");
        if(!success) {
            throw ggapi::GgApiError(
                "ggapi::AuthorizationException",
                "Principal " + principal + " is not authorized to perform " + destination + ":"
                    + operation + " on resource " + resource);
        }
        return ggapi::Struct::create();
    });
}

AuthorizationHandler::AuthorizationHandler() noexcept {
    _authModule = std::make_unique<AuthorizationModule>();
    _policyParser = std::make_unique<AuthorizationPolicyParser>();
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

    for(const auto &acl : componentNameToPolicies) {
        loadAuthorizationPolicies(acl.first, acl.second, false);
    }

    checkAuthZListenerStart();

    // TODO: Add watcher on config changes to the service accessControl block
}

bool AuthorizationHandler::checkAuthZListenerStart() {
    _requestAuthZSub = ggapi::Subscription::subscribeToTopic(
        keys.checkAuthorizationTopic,
        ggapi::TopicCallback::of(&AuthorizationHandler::checkAuthorized, this));

    return true;
}

bool AuthorizationHandler::isAuthorized(
    std::string destination,
    Permission permission,
    ResourceLookupPolicy resourceLookupPolicy) {

    std::string principal = permission.principal;
    std::string operation = permission.operation;
    std::string resource = permission.resource;

    // service name to be lower case
    std::transform(principal.begin(), principal.end(), principal.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    std::vector<std::vector<std::string>> combinations = {
        {destination, principal, operation, resource},
        {destination, principal, AuthorizationModule::ANY_REGEX, resource},
        {destination, AuthorizationModule::ANY_REGEX, operation, resource},
        {destination, AuthorizationModule::ANY_REGEX, AuthorizationModule::ANY_REGEX, resource},
    };
    try {
        for(auto combination : combinations) {
            // Lookup all possible allow configurations starting from most specific to least
            // This helps for access logs, as customer can figure out which policy is being hit.
            // Check each specified combination of provided data, and '*' for lpc calling component
            // and lpc operation.
            Permission newPermission = Permission(combination[1], combination[2], combination[3]);
            if(_authModule->isPresent(combination[0], newPermission, resourceLookupPolicy)) {
                LOG.atDebug().log(
                    "Hit policy with principal " + combination[1] + ",  operation " + combination[2]
                    + ", resource " + combination[3]);
                return true;
            }
        }
        throw AuthorizationException("Principal " + principal + " is not authorized to perform " + destination + ":"
            + operation + " on resource " + resource);
    } catch(const AuthorizationException &e) {
        LOG.atError().log(e.what());
        throw AuthorizationException(e.what());
    }
    return false;
}

void AuthorizationHandler::loadAuthorizationPolicies(
    const std::string &componentName,
    const std::vector<AuthorizationPolicy> &policies,
    bool isUpdate) {
    // load parsed authorization policies into the authorization module. isUpdate is for when a
    // (future) watcher sees an update to an access control block of a service, and needs to re-run
    // the loading. False on startup.
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

    for(const auto &policy : policies) {
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
    for(const auto &policy : policies) {
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
    const std::string &componentName, const AuthorizationPolicy &policy) {
    if(policy.operations.empty()) {
        throw AuthorizationException(
            "Malformed policy with invalid/empty operations: " + policy.policyId);
    }
    // TODO: check if operations is valid and registered?
}

void AuthorizationHandler::validatePolicyId(const std::vector<AuthorizationPolicy> &policies) {
    for(const auto &policy : policies) {
        if(policy.policyId.empty()) {
            throw AuthorizationException("Malformed policy with empty/null policy IDs");
        }
    }
}

void AuthorizationHandler::validatePrincipals(const AuthorizationPolicy &policy) {
    std::vector<std::string> principals = policy.principals;
    if(principals.empty()) {
        throw AuthorizationException(
            "Malformed policy with invalid/empty principal: " + policy.policyId);
    }
    // TODO: check if principal is a valid EG component
}

void AuthorizationHandler::addPermission(
    const std::string &destination,
    const std::string &policyId,
    const std::vector<std::string> &principals,
    const std::vector<std::string> &operations,
    const std::vector<std::string> &resources) noexcept {
    for(const auto &principal : principals) {
        for(const auto &operation : operations) {
            if(resources.empty()) {
                Permission permission = Permission(principal, operation);
                _authModule->addPermission(destination, permission);
            } else {
                for(const auto &resource : resources) {
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
                            .log(std::string("Error while adding permission for component ")
                                     .append(principal)
                                     .append(" to IPC service ")
                                     .append(destination));
                    }
                }
            }
        }
    }
}
