#pragma once
#include "config/config_manager.hpp"
#include "config/validator.hpp"
#include "data/string_table.hpp"
#include "lifecycle/kernel.hpp"
#include "util/nucleus_paths.hpp"
#include <atomic>

namespace data {
    class Environment;
}

namespace lifecycle {
    class Kernel;
    class KernelAlternatives;
    class CommandLine;
} // namespace lifecycle

namespace config {
    class Topics;
    class Validator;
} // namespace config

namespace deployment {
    struct DeviceConfigConsts {
        data::StringOrdInit DEFAULT_NUCLEUS_COMPONENT_NAME{"aws.greengrass.Nucleus-lite"};

        data::StringOrdInit DEVICE_PARAM_THING_NAME{"thingName"};
        data::StringOrdInit DEVICE_PARAM_GG_DATA_ENDPOINT{"greengrassDataPlaneEndpoint"};
        data::StringOrdInit DEVICE_PARAM_IOT_DATA_ENDPOINT{"iotDataEndpoint"};
        data::StringOrdInit DEVICE_PARAM_IOT_CRED_ENDPOINT{"iotCredEndpoint"};
        data::StringOrdInit DEVICE_PARAM_PRIVATE_KEY_PATH{"privateKeyPath"};
        data::StringOrdInit DEVICE_PARAM_CERTIFICATE_FILE_PATH{"certificateFilePath"};
        data::StringOrdInit DEVICE_PARAM_ROOT_CA_PATH{"rootCaPath"};
        data::StringOrdInit DEVICE_PARAM_INTERPOLATE_COMPONENT_CONFIGURATION{
            "interpolateComponentConfiguration"};
        data::StringOrdInit DEVICE_PARAM_IPC_SOCKET_PATH{"ipcSocketPath"};
        data::StringOrdInit SYSTEM_NAMESPACE_KEY{"system"};
        data::StringOrdInit PLATFORM_OVERRIDE_TOPIC{"platformOverride"};
        data::StringOrdInit DEVICE_PARAM_AWS_REGION{"awsRegion"};
        data::StringOrdInit DEVICE_PARAM_FIPS_MODE{"fipsMode"};
        data::StringOrdInit DEVICE_MQTT_NAMESPACE{"mqtt"};
        data::StringOrdInit DEVICE_SPOOLER_NAMESPACE{"spooler"};
        data::StringOrdInit RUN_WITH_TOPIC{"runWithDefault"};
        data::StringOrdInit RUN_WITH_DEFAULT_POSIX_USER{"posixUser"};
        data::StringOrdInit RUN_WITH_DEFAULT_WINDOWS_USER{"windowsUser"};
        data::StringOrdInit RUN_WITH_DEFAULT_POSIX_SHELL{"posixShell"};
        data::StringOrdInit RUN_WITH_DEFAULT_POSIX_SHELL_VALUE{"sh"};
        data::StringOrdInit FLEET_STATUS_CONFIG_TOPICS{"fleetStatus"};

        data::StringOrdInit IOT_ROLE_ALIAS_TOPIC{"iotRoleAlias"};
        data::StringOrdInit COMPONENT_STORE_MAX_SIZE_BYTES{"componentStoreMaxSizeBytes"};
        data::StringOrdInit DEPLOYMENT_POLLING_FREQUENCY_SECONDS = {
            "deploymentPollingFrequencySeconds"};
        data::StringOrdInit NUCLEUS_CONFIG_LOGGING_TOPICS{"logging"};
        data::StringOrdInit TELEMETRY_CONFIG_LOGGING_TOPICS{"telemetry"};

        data::StringOrdInit S3_ENDPOINT_TYPE{"s3EndpointType"};
        //        data::StringOrdInit S3_ENDPOINT_PROP_NAME =
        //            SdkSystemSetting.AWS_S3_US_EAST_1_REGIONAL_ENDPOINT.property();
        data::StringOrdInit DEVICE_NETWORK_PROXY_NAMESPACE{"networkProxy"};
        data::StringOrdInit DEVICE_PROXY_NAMESPACE{"proxy"};
        data::StringOrdInit DEVICE_PARAM_NO_PROXY_ADDRESSES{"noProxyAddresses"};
        data::StringOrdInit DEVICE_PARAM_PROXY_URL{"url"};
        data::StringOrdInit DEVICE_PARAM_PROXY_USERNAME{"username"};
        data::StringOrdInit DEVICE_PARAM_PROXY_PASSWORD{"password"};

        data::StringOrdInit DEVICE_PARAM_GG_DATA_PLANE_PORT{"greengrassDataPlanePort"};

        data::StringOrdInit DEVICE_PARAM_ENV_STAGE{"envStage"};
        data::StringOrdInit DEFAULT_ENV_STAGE{"prod"};
        data::StringOrdInit CANNOT_BE_EMPTY{"cannot be empty"};
        data::StringOrdInit AWS_IOT_THING_NAME_ENV{"AWS_IOT_THING_NAME"};
        data::StringOrdInit GGC_VERSION_ENV{"GGC_VERSION"};
        data::StringOrdInit NUCLEUS_BUILD_METADATA_DIRECTORY{"conf"};
        data::StringOrdInit NUCLEUS_RECIPE_FILENAME{"recipe.yaml"};
        data::StringOrdInit FALLBACK_DEFAULT_REGION{"us-east-1"};
        data::StringOrdInit AMAZON_DOMAIN_SEQUENCE{".amazonaws."};
        data::StringOrdInit FALLBACK_VERSION{"0.0.0"};

        explicit DeviceConfigConsts(data::Environment &environment) {
            data::StringOrdInit::init(
                environment,
                {DEFAULT_NUCLEUS_COMPONENT_NAME,
                 DEVICE_PARAM_THING_NAME,
                 DEVICE_PARAM_GG_DATA_ENDPOINT,
                 DEVICE_PARAM_IOT_DATA_ENDPOINT,
                 DEVICE_PARAM_IOT_CRED_ENDPOINT,
                 DEVICE_PARAM_PRIVATE_KEY_PATH,
                 DEVICE_PARAM_CERTIFICATE_FILE_PATH,
                 DEVICE_PARAM_ROOT_CA_PATH,
                 DEVICE_PARAM_INTERPOLATE_COMPONENT_CONFIGURATION,
                 DEVICE_PARAM_IPC_SOCKET_PATH,
                 SYSTEM_NAMESPACE_KEY,
                 PLATFORM_OVERRIDE_TOPIC,
                 DEVICE_PARAM_AWS_REGION,
                 DEVICE_PARAM_FIPS_MODE,
                 DEVICE_MQTT_NAMESPACE,
                 DEVICE_SPOOLER_NAMESPACE,
                 RUN_WITH_TOPIC,
                 RUN_WITH_DEFAULT_POSIX_USER,
                 RUN_WITH_DEFAULT_WINDOWS_USER,
                 RUN_WITH_DEFAULT_POSIX_SHELL,
                 RUN_WITH_DEFAULT_POSIX_SHELL_VALUE,
                 FLEET_STATUS_CONFIG_TOPICS,
                 IOT_ROLE_ALIAS_TOPIC,
                 COMPONENT_STORE_MAX_SIZE_BYTES,
                 DEPLOYMENT_POLLING_FREQUENCY_SECONDS,
                 NUCLEUS_CONFIG_LOGGING_TOPICS,
                 TELEMETRY_CONFIG_LOGGING_TOPICS,
                 S3_ENDPOINT_TYPE,
                 // S3_ENDPOINT_PROP_NAME
                 DEVICE_NETWORK_PROXY_NAMESPACE,
                 DEVICE_PROXY_NAMESPACE,
                 DEVICE_PARAM_NO_PROXY_ADDRESSES,
                 DEVICE_PARAM_PROXY_URL,
                 DEVICE_PARAM_PROXY_USERNAME,
                 DEVICE_PARAM_PROXY_PASSWORD,
                 DEVICE_PARAM_GG_DATA_PLANE_PORT,
                 DEVICE_PARAM_ENV_STAGE,
                 DEFAULT_ENV_STAGE,
                 CANNOT_BE_EMPTY,
                 AWS_IOT_THING_NAME_ENV,
                 GGC_VERSION_ENV,
                 NUCLEUS_BUILD_METADATA_DIRECTORY,
                 NUCLEUS_RECIPE_FILENAME,
                 FALLBACK_DEFAULT_REGION,
                 AMAZON_DOMAIN_SEQUENCE,
                 FALLBACK_VERSION}
            );
        }
    };

    class DeviceConfiguration {
        mutable std::shared_mutex _mutex;
        data::Environment &_environment;
        lifecycle::Kernel &_kernel;
        std::string _nucleusComponentNameCache;
        std::shared_ptr<util::NucleusPaths> _nucleusPaths;
        std::atomic_bool _deviceConfigValidationCachedResult{false};
        std::shared_ptr<config::Topics> _loggingTopics;

    public:
        const DeviceConfigConsts configs;
        static constexpr uint64_t COMPONENT_STORE_MAX_SIZE_DEFAULT_BYTES = 10'000'000'000L;
        static constexpr long DEPLOYMENT_POLLING_FREQUENCY_DEFAULT_SECONDS = 15L;
        static constexpr uint64_t GG_DATA_PLANE_PORT_DEFAULT = 8443;

        DeviceConfiguration(data::Environment &environment, lifecycle::Kernel &kernel);
        DeviceConfiguration(const DeviceConfiguration &) = delete;
        DeviceConfiguration &operator=(const DeviceConfiguration &) = delete;
        DeviceConfiguration(DeviceConfiguration &&) noexcept = delete;
        DeviceConfiguration &operator=(DeviceConfiguration &&) noexcept = delete;
        ~DeviceConfiguration() = default;

        // TODO: Refactor into new classes and implement
        std::string getNucleusComponentName();
        std::shared_ptr<config::Topics> getLoggingConfigurationTopics();
        std::shared_ptr<config::Topics> getTelemetryConfigurationTopics();
        std::shared_ptr<config::Topics> getStatusConfigurationTopics();
        std::string initNucleusComponentName();
        void initializeNucleusComponentConfig(std::string);
        void persistInitialLaunchParams(lifecycle::KernelAlternatives);
        void initializeNucleusLifecycleConfig(std::string);
        void initializeNucleusVersion(std::string, std::string);
        void initializeComponentStore(
            lifecycle::KernelAlternatives,
            std::string,
            std::string,
            std::filesystem::path,
            std::filesystem::path
        );
        void initializeNucleusFromRecipe(lifecycle::KernelAlternatives);
        void
        copyUnpackedNucleusArtifacts(const std::filesystem::path &, const std::filesystem::path &);
        void handleLoggingConfig();
        void
        handleLoggingConfigurationChanges(config::WhatHappened, const std::shared_ptr<config::ConfigNode> &);
        //        void reconfigureLogging(LogConfigUpdate);
        std::optional<std::string> getComponentType(std::string);
        //        std::shared_ptr<config::Validator> &getDeTildeValidator(lifecycle::CommandLine
        //        &commandLine); std::shared_ptr<config::Validator>
        //        &getRegionValidator(lifecycle::CommandLine &commandLine);
        std::shared_ptr<config::Topics> getRunWithTopic();
        config::Topic getRunWithDefaultPosixUser();
        config::Topic getRunWithDefaultPosixShell();
        config::Topic getRunWithDefaultWindowsUser();
        std::shared_ptr<config::Topics> findRunWithDefaultSystemResourceLimits();
        std::shared_ptr<config::Topics> getPlatformOverrideTopic();
        config::Topic getThingName();
        config::Topic getCertificateFilePath();
        config::Topic getPrivateKeyFilePath();
        config::Topic getRootCAFilePath();
        std::optional<config::Topic> getIpcSocketPath();
        config::Topic getInterpolateComponentConfiguration();
        config::Topic getGGDataEndpoint();
        config::Topic getIoTDataEndpoint();
        config::Topic getIotCredentialEndpoint();
        config::Topic getAWSRegion();
        config::Topic getFipsMode();
        config::Topic getGreengrassDataPlanePort();
        void setAwsRegion(std::string);
        config::Topic getEnvironmentStage();
        std::shared_ptr<config::Topics> getMQTTNamespace();
        std::shared_ptr<config::Topics> getSpoolerNamespace();
        std::shared_ptr<config::Topics> getNetworkProxyNamespace();
        std::shared_ptr<config::Topics> getProxyNamespace();
        std::string getNoProxyAddresses();
        std::string getProxyUrl();
        std::string getProxyUsername();
        std::string getProxyPassword();
        config::Topic getIotRoleAlias();
        config::Topic getComponentStoreMaxSizeBytes();
        config::Topic getDeploymentPollingFrequencySeconds();
        config::Topic gets3EndpointType();
        // void onAnyChange(ChildChanged);
        void validate();
        void validate(bool);
        bool isDeviceConfiguredToTalkToCloud();
        bool provisionInfoNodeChanged(const std::shared_ptr<config::ConfigNode> &node, bool);
        config::Topic getTopic(data::StringOrdInit);
        std::shared_ptr<config::Topics> getTopics(data::StringOrdInit);
        std::string getNucleusVersion();
        std::string getVersionFromBuildRecipeFile();
        void validateDeviceConfiguration(
            std::string_view,
            std::string_view,
            std::string_view,
            std::string_view,
            std::string_view,
            std::string_view,
            std::string_view,
            bool
        );
        void validateEndpoints(std::string_view, std::string_view, std::string_view);
        // LogConfigUpdate fromPojo(std::unordered_map<std::string, >);
        // KeyManager[] getDeviceIdentityKeyManagers();
        std::shared_ptr<config::Topics> getHttpClientOptions();
    };
} // namespace deployment
