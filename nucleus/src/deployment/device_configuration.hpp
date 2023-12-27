#pragma once
#include "config/config_manager.hpp"
#include "data/string_table.hpp"
#include "errors/error_base.hpp"
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

namespace logging {
    class LogConfigUpdate;
}

namespace deployment {
    struct DeviceConfigConsts {
        data::SymbolInit DEFAULT_NUCLEUS_COMPONENT_NAME{"aws.greengrass.Nucleus-lite"};

        data::SymbolInit DEVICE_PARAM_THING_NAME{"thingName"};
        data::SymbolInit DEVICE_PARAM_GG_DATA_ENDPOINT{"greengrassDataPlaneEndpoint"};
        data::SymbolInit DEVICE_PARAM_IOT_DATA_ENDPOINT{"iotDataEndpoint"};
        data::SymbolInit DEVICE_PARAM_IOT_CRED_ENDPOINT{"iotCredEndpoint"};
        data::SymbolInit DEVICE_PARAM_PRIVATE_KEY_PATH{"privateKeyPath"};
        data::SymbolInit DEVICE_PARAM_CERTIFICATE_FILE_PATH{"certificateFilePath"};
        data::SymbolInit DEVICE_PARAM_ROOT_CA_PATH{"rootCaPath"};
        data::SymbolInit DEVICE_PARAM_INTERPOLATE_COMPONENT_CONFIGURATION{
            "interpolateComponentConfiguration"};
        data::SymbolInit DEVICE_PARAM_IPC_SOCKET_PATH{"ipcSocketPath"};
        data::SymbolInit SYSTEM_NAMESPACE_KEY{"system"};
        data::SymbolInit SERVICES_NAMESPACE_KEY{"services"};
        data::SymbolInit PLATFORM_OVERRIDE_TOPIC{"platformOverride"};
        data::SymbolInit DEVICE_PARAM_AWS_REGION{"awsRegion"};
        data::SymbolInit DEVICE_PARAM_FIPS_MODE{"fipsMode"};
        data::SymbolInit DEVICE_MQTT_NAMESPACE{"mqtt"};
        data::SymbolInit DEVICE_SPOOLER_NAMESPACE{"spooler"};
        data::SymbolInit RUN_WITH_TOPIC{"runWithDefault"};
        data::SymbolInit RUN_WITH_DEFAULT_POSIX_USER{"posixUser"};
        data::SymbolInit RUN_WITH_DEFAULT_WINDOWS_USER{"windowsUser"};
        data::SymbolInit RUN_WITH_DEFAULT_POSIX_SHELL{"posixShell"};
        data::SymbolInit RUN_WITH_DEFAULT_POSIX_SHELL_VALUE{"sh"};
        data::SymbolInit FLEET_STATUS_CONFIG_TOPICS{"fleetStatus"};

        data::SymbolInit IOT_ROLE_ALIAS_TOPIC{"iotRoleAlias"};
        data::SymbolInit COMPONENT_STORE_MAX_SIZE_BYTES{"componentStoreMaxSizeBytes"};
        data::SymbolInit DEPLOYMENT_POLLING_FREQUENCY_SECONDS = {
            "deploymentPollingFrequencySeconds"};
        data::SymbolInit NUCLEUS_CONFIG_LOGGING_TOPICS{"logging"};
        data::SymbolInit TELEMETRY_CONFIG_LOGGING_TOPICS{"telemetry"};
        data::SymbolInit CONFIGURATION_CONFIG_KEY{"configuration"};

        data::SymbolInit S3_ENDPOINT_TYPE{"s3EndpointType"};
        //        data::StringOrdInit S3_ENDPOINT_PROP_NAME =
        //            SdkSystemSetting.AWS_S3_US_EAST_1_REGIONAL_ENDPOINT.property();
        data::SymbolInit DEVICE_NETWORK_PROXY_NAMESPACE{"networkProxy"};
        data::SymbolInit DEVICE_PROXY_NAMESPACE{"proxy"};
        data::SymbolInit DEVICE_PARAM_NO_PROXY_ADDRESSES{"noProxyAddresses"};
        data::SymbolInit DEVICE_PARAM_PROXY_URL{"url"};
        data::SymbolInit DEVICE_PARAM_PROXY_USERNAME{"username"};
        data::SymbolInit DEVICE_PARAM_PROXY_PASSWORD{"password"};

        data::SymbolInit DEVICE_PARAM_GG_DATA_PLANE_PORT{"greengrassDataPlanePort"};

        data::SymbolInit DEVICE_PARAM_ENV_STAGE{"envStage"};
        data::SymbolInit DEFAULT_ENV_STAGE{"prod"};
        data::SymbolInit AWS_IOT_THING_NAME_ENV{"AWS_IOT_THING_NAME"};
        data::SymbolInit GGC_VERSION_ENV{"GGC_VERSION"};
        data::SymbolInit HTTP_CLIENT{"httpClient"};

        // Strings that are not used as keys
        constexpr static std::string_view CANNOT_BE_EMPTY{"cannot be empty"};
        constexpr static std::string_view NUCLEUS_BUILD_METADATA_DIRECTORY{"conf"};
        constexpr static std::string_view NUCLEUS_RECIPE_FILENAME{"recipe.yaml"};
        constexpr static std::string_view FALLBACK_DEFAULT_REGION{"us-east-1"};
        constexpr static std::string_view AMAZON_DOMAIN_SEQUENCE{".amazonaws."};
        constexpr static std::string_view FALLBACK_VERSION{"0.0.0"};

        explicit DeviceConfigConsts(const scope::UsingContext &context) {
            data::SymbolInit::init(
                context,
                {
                    &DEFAULT_NUCLEUS_COMPONENT_NAME,
                    &DEVICE_PARAM_THING_NAME,
                    &DEVICE_PARAM_GG_DATA_ENDPOINT,
                    &DEVICE_PARAM_IOT_DATA_ENDPOINT,
                    &DEVICE_PARAM_IOT_CRED_ENDPOINT,
                    &DEVICE_PARAM_PRIVATE_KEY_PATH,
                    &DEVICE_PARAM_CERTIFICATE_FILE_PATH,
                    &DEVICE_PARAM_ROOT_CA_PATH,
                    &DEVICE_PARAM_INTERPOLATE_COMPONENT_CONFIGURATION,
                    &DEVICE_PARAM_IPC_SOCKET_PATH,
                    &SYSTEM_NAMESPACE_KEY,
                    &PLATFORM_OVERRIDE_TOPIC,
                    &DEVICE_PARAM_AWS_REGION,
                    &DEVICE_PARAM_FIPS_MODE,
                    &DEVICE_MQTT_NAMESPACE,
                    &DEVICE_SPOOLER_NAMESPACE,
                    &RUN_WITH_TOPIC,
                    &RUN_WITH_DEFAULT_POSIX_USER,
                    &RUN_WITH_DEFAULT_WINDOWS_USER,
                    &RUN_WITH_DEFAULT_POSIX_SHELL,
                    &RUN_WITH_DEFAULT_POSIX_SHELL_VALUE,
                    &FLEET_STATUS_CONFIG_TOPICS,
                    &IOT_ROLE_ALIAS_TOPIC,
                    &COMPONENT_STORE_MAX_SIZE_BYTES,
                    &DEPLOYMENT_POLLING_FREQUENCY_SECONDS,
                    &NUCLEUS_CONFIG_LOGGING_TOPICS,
                    &TELEMETRY_CONFIG_LOGGING_TOPICS,
                    &CONFIGURATION_CONFIG_KEY,
                    &S3_ENDPOINT_TYPE,
                    // &S3_ENDPOINT_PROP_NAME
                    &DEVICE_NETWORK_PROXY_NAMESPACE,
                    &DEVICE_PROXY_NAMESPACE,
                    &DEVICE_PARAM_NO_PROXY_ADDRESSES,
                    &DEVICE_PARAM_PROXY_URL,
                    &DEVICE_PARAM_PROXY_USERNAME,
                    &DEVICE_PARAM_PROXY_PASSWORD,
                    &DEVICE_PARAM_GG_DATA_PLANE_PORT,
                    &DEVICE_PARAM_ENV_STAGE,
                    &DEFAULT_ENV_STAGE,
                    &AWS_IOT_THING_NAME_ENV,
                    &GGC_VERSION_ENV,
                    &HTTP_CLIENT,
                });
        }
    };

    class DeviceConfigurationException : public errors::Error {

    public:
        explicit DeviceConfigurationException(const std::string &msg) noexcept
            : Error("DeviceConfigurationException", msg) {
        }
    };

    class DeviceConfiguration : public util::RefObject<DeviceConfiguration>, scope::UsesContext {
        mutable std::shared_mutex _mutex;
        lifecycle::Kernel &_kernel;
        std::string _nucleusComponentNameCache;
        std::shared_ptr<util::NucleusPaths> _nucleusPaths;
        std::atomic_bool _deviceConfigValidationCachedResult{false};

        void initialize();

    public:
        const DeviceConfigConsts configs;
        static constexpr uint64_t COMPONENT_STORE_MAX_SIZE_DEFAULT_BYTES = 10'000'000'000L;
        static constexpr long DEPLOYMENT_POLLING_FREQUENCY_DEFAULT_SECONDS = 15L;
        static constexpr uint64_t GG_DATA_PLANE_PORT_DEFAULT = 8443;

        DeviceConfiguration(const scope::UsingContext &context, lifecycle::Kernel &kernel);
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
        void initializeNucleusComponentConfig(const std::string &);
        void persistInitialLaunchParams(lifecycle::KernelAlternatives &);
        void initializeNucleusLifecycleConfig(const std::string &);
        void initializeNucleusVersion(const std::string &, const std::string &);
        void initializeComponentStore(
            lifecycle::KernelAlternatives &,
            const std::string &,
            const std::string &,
            const std::filesystem::path &,
            const std::filesystem::path &);
        void copyUnpackedNucleusArtifacts(
            const std::filesystem::path &, const std::filesystem::path &);
        void handleLoggingConfig();
        void handleLoggingConfigurationChanges(
            const std::shared_ptr<config::Topics> &topics,
            data::Symbol key,
            config::WhatHappened changeType);
        std::optional<std::string> getComponentType(const std::string &);
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
        void setAwsRegion(const std::string &);
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
        void validateConfiguration();
        void validateConfiguration(bool);
        bool isDeviceConfiguredToTalkToCloud();
        bool provisionInfoNodeChanged(const std::shared_ptr<config::ConfigNode> &node, bool);
        config::Topic getTopic(data::Symbol);
        std::shared_ptr<config::Topics> getTopics(data::Symbol);
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
            bool);
        void validateEndpoints(std::string_view, std::string_view, std::string_view);
        // LogConfigUpdate fromPojo(std::unordered_map<std::string, >);
        // KeyManager[] getDeviceIdentityKeyManagers();
        std::shared_ptr<config::Topics> getHttpClientOptions();
        void onAnyChange(const std::shared_ptr<config::Watcher> &watcher);
        void invalidateCachedResult();
        static std::shared_ptr<DeviceConfiguration> create(
            const scope::UsingContext &context, lifecycle::Kernel &kernel);
    };
} // namespace deployment
