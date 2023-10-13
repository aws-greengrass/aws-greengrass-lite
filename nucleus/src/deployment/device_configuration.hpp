#pragma once
#include "data/string_table.hpp"
#include <atomic>

namespace data {
    class Environment;
}

namespace lifecycle {
    class Kernel;
}

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
        data::StringOrdInit DEPLOYMENT_POLLING_FREQUENCY_SECONDS =
            "deploymentPollingFrequencySeconds";
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
        data::StringOrdInit CANNOT_BE_EMPTY{" cannot be empty"};
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
        std::atomic_bool _deviceConfigValidationCachedResult;

    public:
        const DeviceConfigConsts names;
        static constexpr uint64_t COMPONENT_STORE_MAX_SIZE_DEFAULT_BYTES = 10'000'000'000L;
        static constexpr long DEPLOYMENT_POLLING_FREQUENCY_DEFAULT_SECONDS = 15L;
        static constexpr int GG_DATA_PLANE_PORT_DEFAULT = 8443;

        DeviceConfiguration(data::Environment &environment, lifecycle::Kernel &kernel)
            : _environment(environment), _kernel(kernel), names(environment) {
            // TODO: deTildeValidator
            // TODO: regionValidator
            // TODO: loggingConfig
            // TODO: componentMaxSizeBytes
            // TODO: pollingFreqSeconds
            // TODO: S3-Endpoint-name
            // TODO: OnAnyChange
        }

        std::string getNucleusComponentName() {
            std::shared_lock shared_guard{_mutex};
            if(!_nucleusComponentNameCache.empty()) {
                if(_kernel.findServiceTopic(_nucleusComponentNameCache)) {
                    return _nucleusComponentNameCache;
                }
            }
            shared_guard.unlock();
            std::unique_lock guard{_mutex};
            if(!_nucleusComponentNameCache.empty()) {
                return _nucleusComponentNameCache;
            }
        }
    };
} // namespace deployment
