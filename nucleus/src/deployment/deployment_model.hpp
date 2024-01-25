#pragma once
#include "data/string_table.hpp"
#include <cstdint>
#include <util.hpp>

namespace deployment {
    inline static const data::SymbolInit CREATE_DEPLOYMENT_TOPIC_NAME{
        "aws.greengrass.deployment.Offer"};
    inline static const data::SymbolInit CANCEL_DEPLOYMENT_TOPIC_NAME{
        "aws.greengrass.deployment.Cancel"};

    enum class DeploymentType : uint32_t { IOT_JOBS, LOCAL, SHADOW };

    inline static const std::unordered_map<std::string, DeploymentType> DeploymentTypeMap{
        {"IOT_JOBS", DeploymentType::IOT_JOBS},
        {"LOCAL", DeploymentType::LOCAL},
        {"SHADOW", DeploymentType::SHADOW}};

    enum class DeploymentStage : uint32_t {
        /**
         * Deployment workflow is non-intrusive, i.e. not impacting kernel runtime
         */
        DEFAULT,

        /**
         * Deployment goes over component bootstrap steps, which can be intrusive to kernel.
         */
        BOOTSTRAP,

        /**
         * Deployment has finished bootstrap steps and is in the middle of applying all changes to
         * Kernel.
         */
        KERNEL_ACTIVATION,

        /**
         * Deployment tries to rollback to Kernel with previous configuration, after BOOTSTRAP or
         * KERNEL_ACTIVATION fails.
         */
        KERNEL_ROLLBACK,

        /**
         * Deployment executes component bootstrap steps for the rollback, after BOOTSTRAP or
         * KERNEL_ACTIVATION fails. Only used when a specific config flag has been set for one or
         * more components in the rollback set.
         */
        ROLLBACK_BOOTSTRAP
    };

    inline static const std::unordered_map<std::string, DeploymentStage> DeploymentStageMap{
        {"DEFAULT", DeploymentStage::DEFAULT},
        {"BOOTSTRAP", DeploymentStage::BOOTSTRAP},
        {"KERNEL_ACTIVATION", DeploymentStage::KERNEL_ACTIVATION},
        {"KERNEL_ROLLBACK", DeploymentStage::KERNEL_ROLLBACK},
        {"ROLLBACK_BOOTSTRAP", DeploymentStage::ROLLBACK_BOOTSTRAP},
    };

    struct DeploymentConsts {
        inline static const data::SymbolInit DEFAULT_SYM{"DEFAULT"};
        inline static const data::SymbolInit BOOTSTRAP_SYM{"BOOTSTRAP"};
        inline static const data::SymbolInit KERNEL_ACTIVATION_SYM{"KERNEL_ACTIVATION"};
        inline static const data::SymbolInit KERNEL_ROLLBACK_SYM{"KERNEL_ROLLBACK"};
        inline static const data::SymbolInit ROLLBACK_BOOTSTRAP_SYM{"ROLLBACK_BOOTSTRAP"};
        inline static const util::LookupTable<data::Symbol, DeploymentStage, 5> STAGE_MAP{
            DEFAULT_SYM,
            DeploymentStage::DEFAULT,
            BOOTSTRAP_SYM,
            DeploymentStage::BOOTSTRAP,
            KERNEL_ACTIVATION_SYM,
            DeploymentStage::KERNEL_ACTIVATION,
            KERNEL_ROLLBACK_SYM,
            DeploymentStage::KERNEL_ROLLBACK,
            ROLLBACK_BOOTSTRAP_SYM,
            DeploymentStage::ROLLBACK_BOOTSTRAP,
        };
    };
    struct DeploymentPackageConfig {
        std::string packageName;
        bool rootComponent;
        std::string resolvedVersion;
        //        ConfigUpdateOperation configUpdateOperation;
        //        RunWith runWith;
    };
    struct DeploymentDocument {
        std::string requestId;
        std::string requestTimestamp;
        std::string componentsToMerge;
        std::string componentsToRemove;
        std::string recipeDirectoryPath;
        std::string artifactsDirectoryPath;
        std::string configurationArn;
        std::vector<DeploymentPackageConfig> deploymentPackageConfig;
        std::vector<std::string> requiredCapabilities;
        std::string groupName;
        std::string onBehalfOf;
        std::string parentGroupName;
        long long int timestamp; // TODO: timepoint
        //        FailureHandlingPolicy failureHandlingPolicy;
        //        ComponentUpdatePolicy componentUpdatePolicy;
        //        DeploymentConfigValidationPolicy deploymentConfigValidationPolicy;
    };

    struct Deployment {
        // deploymentDocumentobj
        DeploymentDocument deploymentDocument;
        DeploymentType deploymentType;
        std::string id;
        bool isCancelled;
        DeploymentStage deploymentStage;
        std::string stageDetails;
    };

    struct Command {
        bool requiresPrivilege;
        std::string script;
    };

    struct ComponentConfiguration {
        std::string message;
    };

    struct Recipe {
        std::string formatVersion;
        std::string componentName;
        std::string componentVersion;
        std::string description;
        std::string publisher;
        ComponentConfiguration configuration;
        Command runCommand;
        Command installCommand;
    };
} // namespace deployment
