#pragma once
#include "data/serializable.hpp"
#include "data/string_table.hpp"
#include <cstdint>
#include <forward_list>
#include <util.hpp>

namespace deployment {
    inline static const data::SymbolInit CREATE_DEPLOYMENT_TOPIC_NAME{
        "aws.greengrass.deployment.Offer"};
    inline static const data::SymbolInit CANCEL_DEPLOYMENT_TOPIC_NAME{
        "aws.greengrass.deployment.Cancel"};

    enum class DeploymentType : uint32_t { IOT_JOBS, LOCAL, SHADOW };

    static constexpr std::string_view IOT_JOBS{"IOT_JOBS"};
    static constexpr std::string_view LOCAL{"LOCAL"};
    static constexpr std::string_view SHADOW{"SHADOW"};

    inline static const util::LookupTable<std::string_view, DeploymentType, 3> DeploymentTypeMap{
        IOT_JOBS,
        DeploymentType::IOT_JOBS,
        LOCAL,
        DeploymentType::LOCAL,
        SHADOW,
        DeploymentType::SHADOW};

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

    inline static const util::LookupTable<std::string_view, DeploymentStage, 5> DeploymentStageMap{
        "DEFAULT",
        DeploymentStage::DEFAULT,
        "BOOTSTRAP",
        DeploymentStage::BOOTSTRAP,
        "KERNEL_ACTIVATION",
        DeploymentStage::KERNEL_ACTIVATION,
        "KERNEL_ROLLBACK",
        DeploymentStage::KERNEL_ROLLBACK,
        "ROLLBACK_BOOTSTRAP",
        DeploymentStage::ROLLBACK_BOOTSTRAP,
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

    enum class FailureHandlingPolicy : uint32_t { ROLLBACK, DO_NOTHING };

    inline static const util::LookupTable<std::string_view, FailureHandlingPolicy, 2>
        FailureHandlingPolicyMap{
            "ROLLBACK",
            FailureHandlingPolicy::ROLLBACK,
            "DO_NOTHING",
            FailureHandlingPolicy::DO_NOTHING,
        };

    enum class DeploymentComponentUpdatePolicyAction : uint32_t {
        NOTIFY_COMPONENTS,
        SKIP_NOTIFY_COMPONENTS,
        UNKNOWN_TO_SDK_VERSION
    };

    inline static const util::
        LookupTable<std::string_view, DeploymentComponentUpdatePolicyAction, 3>
            DeploymentComponentUpdatePolicyActionMap{
                "NOTIFY_COMPONENTS",
                DeploymentComponentUpdatePolicyAction::NOTIFY_COMPONENTS,
                "SKIP_NOTIFY_COMPONENTS",
                DeploymentComponentUpdatePolicyAction::SKIP_NOTIFY_COMPONENTS,
                "null",
                DeploymentComponentUpdatePolicyAction::UNKNOWN_TO_SDK_VERSION,
            };

    struct ComponentUpdatePolicy : public data::Serializable {
        int timeout = 60;
        std::string action;

        void visit(data::Archive &archive) override {
            archive.setIgnoreCase();
            archive("timeout", timeout);
            archive("action", action);
        }
    };

    template<typename T>
    struct SdkField {
        std::string memberName;
        std::string locationName;
        std::string unmarshallLocationName;
        // TODO: rest of the fields
    };

    struct DeploymentConfigValidationPolicy {
        SdkField<int> timeout_in_seconds;
        std::vector<SdkField<int>> sdkFields;
        long long int serialVersionUID = 1;
        int timeoutInSeconds;
    };

    struct SystemResourceLimits {
        long long int memory;
        double cpus;
    };

    struct RunWith {
        std::string posixUser;
        std::string windowsUser;
        SystemResourceLimits systemResourceLimits;
    };

    enum class ConfigUpdateOperation : uint32_t {
        MERGE,
        RESET,
    };

    struct DeploymentPackageConfig {
        std::string packageName;
        bool rootComponent;
        std::string resolvedVersion;
        std::string configUpdateOperation;
        RunWith runWith;
    };

    struct DeploymentDocument : public data::Serializable {
        std::string deploymentId;
        uint64_t timestamp;
        std::unordered_map<std::string, std::string> componentsToMerge;
        std::unordered_map<std::string, std::string> componentsToRemove;
        std::string recipeDirectoryPath;
        std::string artifactsDirectoryPath;
        std::string configurationArn;
        std::vector<DeploymentPackageConfig> deploymentPackageConfig;
        std::vector<std::string> requiredCapabilities;
        std::string groupName;
        std::string onBehalfOf;
        std::string parentGroupName;
        std::string failureHandlingPolicy;
        ComponentUpdatePolicy componentUpdatePolicy;
        DeploymentConfigValidationPolicy deploymentConfigValidationPolicy;

        void visit(data::Archive &archive) override {
            // TODO: Do rest and check names from cli
            archive.setIgnoreCase();
            archive("requestId", deploymentId);
            archive("requestTimestamp", timestamp);
            archive("rootComponentVersionsToAdd", componentsToMerge);
            archive("rootComponentsToRemove", componentsToRemove);
            archive("groupName", groupName);
            archive("parentGroupName", parentGroupName);
            archive("onBehalfof", onBehalfOf);
            archive("configurationArn", configurationArn);
            archive("requiredCapabilities", requiredCapabilities);
            archive("recipeDirectoryPath", recipeDirectoryPath);
            archive("artifactsDirectoryPath", artifactsDirectoryPath);
            archive("failureHandlingPolicy", failureHandlingPolicy);
            archive("componentUpdatePolicy", componentUpdatePolicy);
        }
    };

    struct Deployment {
        // GG-Java: Need both document object and string
        DeploymentDocument deploymentDocumentObj;
        std::string deploymentDocument;
        DeploymentType deploymentType;
        std::string id;
        bool isCancelled;
        DeploymentStage deploymentStage;
        std::string stageDetails;
        std::vector<std::string> errorStack;
        std::vector<std::string> errorTypes;
    };

    enum class DeploymentStatus {
        SUCCESSFUL,
        FAILED_NO_STATE_CHANGE,
        FAILED_ROLLBACK_NOT_REQUESTED,
        FAILED_ROLLBACK_COMPLETE,
        FAILED_UNABLE_TO_ROLLBACK,
        REJECTED
    };

    struct DeploymentResult {
        DeploymentStatus deploymentStatus;
        // DeploymentException deploymentException;
    };
} // namespace deployment
