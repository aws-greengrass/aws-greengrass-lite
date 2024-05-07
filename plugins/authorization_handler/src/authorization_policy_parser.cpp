#include "authorization_policy.hpp"

#include <logging.hpp>
#include <plugin.hpp>

static const auto LOG = ggapi::Logger::of("authorization_handler");

namespace authorization {

    AuthorizationPolicyParser::AuthorizationPolicyParser() = default;

    std::unordered_map<std::string, std::vector<AuthorizationPolicy>>
    AuthorizationPolicyParser::parseAllAuthorizationPolicies(ggapi::Struct configRoot) {
        std::unordered_map<std::string, std::vector<AuthorizationPolicy>>
            _primaryAuthorizationPolicyMap;

        auto allServices = configRoot.get<ggapi::Struct>(configRoot.foldKey("services"));
        if(allServices.empty()) {
            LOG.atWarn("load-authorization-all-services-component-config-retrieval-error")
                .log("Unable to retrieve services config");
            return _primaryAuthorizationPolicyMap;
        }

        for(const auto &serviceKey : allServices.keys().toVector<std::string>()) {
            if(!allServices.hasKey(serviceKey) || !allServices.isStruct(serviceKey)) {
                continue;
            }
            auto service = allServices.get<ggapi::Struct>(allServices.foldKey(serviceKey));
            if(service.empty()) {
                continue;
            }
            // TODO: Get the serviceName (componentName) from the service struct some how
            // For now this parses it as all lower case.
            const auto &componentName = serviceKey;

            if(!service.hasKey("configuration") || !service.isStruct("configuration")) {
                continue;
            }

            auto configurationStruct = service.get<ggapi::Struct>(service.foldKey("configuration"));
            if(configurationStruct.empty()) {
                continue;
            }

            // TODO: accessControl block may need to be injected from gen_components / plugins
            // currently directly in nucleus_config, plugins only have "logging" in configurations
            // struct
            if(!configurationStruct.hasKey("accessControl")
               || !configurationStruct.isStruct("accessControl")) {
                continue;
            }

            auto accessControlStruct = configurationStruct.get<ggapi::Struct>(
                configurationStruct.foldKey("accessControl"));
            if(accessControlStruct.empty()) {
                continue;
            }

            auto componentAuthorizationPolicyMap =
                parseAllPoliciesForComponent(accessControlStruct, componentName);

            for(const auto &it : componentAuthorizationPolicyMap) {
                auto policyType = it.first;
                auto policyList = it.second;

                if(_primaryAuthorizationPolicyMap.find(policyType)
                   == _primaryAuthorizationPolicyMap.end()) {
                    _primaryAuthorizationPolicyMap[policyType] = policyList;
                } else {
                    _primaryAuthorizationPolicyMap[policyType].insert(
                        _primaryAuthorizationPolicyMap[policyType].end(),
                        policyList.begin(),
                        policyList.end());
                }
            }
        }

        return _primaryAuthorizationPolicyMap;
    }

    std::unordered_map<std::string, std::vector<AuthorizationPolicy>>
    AuthorizationPolicyParser::parseAllPoliciesForComponent(
        ggapi::Struct accessControlStruct, const std::string &sourceComponent) {
        std::unordered_map<std::string, std::vector<AuthorizationPolicy>> authorizationPolicyMap;
        std::unordered_map<std::string, std::unordered_map<std::string, AuthorizationPolicyConfig>>
            accessControlMap;

        for(const auto &destination : accessControlStruct.keys().toVector<std::string>()) {
            if(!accessControlStruct.hasKey(destination)
               || !accessControlStruct.isStruct(destination)) {
                continue;
            }
            auto destinationStruct =
                accessControlStruct.get<ggapi::Struct>(accessControlStruct.foldKey(destination));
            if(destinationStruct.empty()) {
                continue;
            }
            std::unordered_map<std::string, AuthorizationPolicyConfig> policyIdMap;
            for(const auto &policyId : destinationStruct.keys().toVector<std::string>()) {
                if(!destinationStruct.hasKey(policyId) || !destinationStruct.isStruct(policyId)) {
                    continue;
                }
                auto policyIdStruct =
                    destinationStruct.get<ggapi::Struct>(destinationStruct.foldKey(policyId));
                if(policyIdStruct.empty()) {
                    continue;
                }

                AuthorizationPolicyConfig _authZPolicyConfig;
                ggapi::Archive::transform<ggapi::ContainerDearchiver>(
                    _authZPolicyConfig, policyIdStruct);
                policyIdMap.insert({policyId, _authZPolicyConfig});
            }
            accessControlMap.insert({destination, policyIdMap});
        }

        for(const auto &accessControl : accessControlMap) {
            std::string destinationComponent = accessControl.first;
            std::unordered_map<std::string, AuthorizationPolicyConfig> accessControlValue =
                accessControl.second;

            auto newAuthorizationPolicyList =
                parseAuthorizationPolicyConfig(sourceComponent, accessControlValue);
            authorizationPolicyMap.insert({destinationComponent, newAuthorizationPolicyList});
        }

        return authorizationPolicyMap;
    }

    std::vector<AuthorizationPolicy> AuthorizationPolicyParser::parseAuthorizationPolicyConfig(
        const std::string &componentName,
        const std::unordered_map<std::string, AuthorizationPolicyConfig> &accessControlConfig) {
        std::vector<AuthorizationPolicy> newAuthorizationPolicyList;

        for(const auto &policyEntryIterator : accessControlConfig) {
            auto authZPolicy = policyEntryIterator.second;
            if(authZPolicy.operations.empty()) {
                std::string err = "Policy operations are missing or invalid";
                LOG.atError("load-authorization-missing-policy-component-operations").log(err);
                continue;
            }
            std::vector<std::string> principals = {componentName};
            // parse the config into the policy list
            AuthorizationPolicy newPolicy = AuthorizationPolicy(
                policyEntryIterator.first,
                authZPolicy.policyDescription,
                principals,
                authZPolicy.operations,
                authZPolicy.resources);
            newAuthorizationPolicyList.push_back(newPolicy);
        }

        return newAuthorizationPolicyList;
    }
} // namespace authorization
