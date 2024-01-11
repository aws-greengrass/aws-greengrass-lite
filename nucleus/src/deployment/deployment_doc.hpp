#pragma once
#include "data/data_model.hpp"
#include <optional>

namespace deployment {
    namespace models {
        class DeploymentPackageConfiguration;
        class FailureHandlingPolicy;
        class ComponentUpdatePolicy;
        class DeploymentConfigurationValidationPolicy;


        struct DeploymentDocument : public data::Model {
            Field<std::string> deploymentId{"DeploymentId", this};
            Field<std::string> configurationArn{"ConfigurationArn", this};
//            Field<DeploymentPackageConfiguration> deploymentPackageConfigurationList{
//                "Packages", this};
            Field<std::string> requiredCapabilities{"RequiredCapabilities", this};
            Field<std::string> groupName{"GroupName", this};
            Field<std::string> onBehalfOf{"OnBehalfOf", this};
            Field<std::string> paretGroupName{"ParentGroupName", this};
            Field<uint64_t> timestamp{"Timestamp", this};
//            EnumField<FailureHandlingPolicy> failureHandlingPolicy{"FailureHandlingPolicy", this};
//            Field<ComponentUpdatePolicy> componentUpdatePolicy{"ComponentUpdatePolicy", this};
//            Field<DeploymentConfigurationValidationPolicy> configurationValidationPolicy{
//                "ConfigurationValidationPolicy", this};
        };
    } // namespace models
} // namespace deployment