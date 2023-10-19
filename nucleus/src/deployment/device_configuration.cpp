#include "device_configuration.hpp"
#include "lifecycle/command_line.hpp"
#include "lifecycle/kernel.hpp"

namespace deployment {

    DeviceConfiguration::DeviceConfiguration(
        data::Environment &environment, lifecycle::Kernel &kernel
    )
        : _environment(environment), _kernel(kernel), configs(environment) {
        // TODO: deTildeValidator
        // TODO: regionValidator
        // TODO: loggingConfig
        getComponentStoreMaxSizeBytes().dflt(configs.COMPONENT_STORE_MAX_SIZE_BYTES);
        getDeploymentPollingFrequencySeconds().dflt(configs.DEPLOYMENT_POLLING_FREQUENCY_SECONDS);
        // TODO: S3-Endpoint-name
        // TODO: OnAnyChange
    }

    std::string DeviceConfiguration::getNucleusComponentName() {
        std::unique_lock guard{_mutex};
        if(!_nucleusComponentNameCache.empty()) {
            if(_kernel.findServiceTopic(_nucleusComponentNameCache)) {
                _nucleusComponentNameCache = initNucleusComponentName();
            }
        }
        return _nucleusComponentNameCache;
    }

    std::shared_ptr<config::Topics> DeviceConfiguration::getLoggingConfigurationTopics() {
        return getTopics(configs.NUCLEUS_CONFIG_LOGGING_TOPICS);
    }

    std::shared_ptr<config::Topics> DeviceConfiguration::getTelemetryConfigurationTopics() {
        return getTopics(configs.TELEMETRY_CONFIG_LOGGING_TOPICS);
    }

    std::shared_ptr<config::Topics> DeviceConfiguration::getStatusConfigurationTopics() {
        return getTopics(configs.FLEET_STATUS_CONFIG_TOPICS);
    }

    std::string DeviceConfiguration::initNucleusComponentName() {
        // TODO: missing code
    }

    void DeviceConfiguration::initializeNucleusComponentConfig(std::string nucleusComponentName) {
        _kernel.getConfig()
            .lookup({"services", nucleusComponentName, _kernel.SERVICES_TOPIC_KEY})
            .dflt("nucleus");
        config::Topic potentialTopic =
            _kernel.getConfig().lookup({"services", "main", "dependencies"});
        if(potentialTopic.getParent()) {
            _kernel.getConfig()
                .lookup({"services", "main", "dependencies"})
                .dflt(potentialTopic.get()); // TODO: add nucleusComponentName
        }
    }

    // Persist initial launch parameters of JVM options.
    void DeviceConfiguration::persistInitialLaunchParams(lifecycle::KernelAlternatives kernelAlts) {
        // TODO: missing code
    }

    void DeviceConfiguration::initializeNucleusLifecycleConfig(std::string) {
        // TODO: missing code
    }

    void DeviceConfiguration::initializeNucleusVersion(
        std::string nucleusComponentName, std::string nucleusComponentVersion
    ) {
        _kernel.getConfig()
            .lookup({"services", nucleusComponentName, "version"})
            .dflt(nucleusComponentVersion);
        _kernel.getConfig()
            .lookup({"setenv", configs.GGC_VERSION_ENV})
            .overrideValue(nucleusComponentVersion);
    }

    void DeviceConfiguration::initializeComponentStore(
        lifecycle::KernelAlternatives,
        std::string,
        std::string,
        std::filesystem::path,
        std::filesystem::path
    ) {
        // TODO: change definition and missing code
    }

    void DeviceConfiguration::initializeNucleusFromRecipe(lifecycle::KernelAlternatives) {
        // TODO: missing code
    }

    void DeviceConfiguration::
        copyUnpackedNucleusArtifacts(const std::filesystem::path &, const std::filesystem::path &) {
        // TODO: missing code
    }

    void DeviceConfiguration::handleLoggingConfig() {
        // TODO: missing code
    }

    void DeviceConfiguration::handleLoggingConfigurationChanges(
        config::WhatHappened, config::ConfigNode
    ) {
        // TODO: missing code
    }

    //    void DeviceConfiguration::reconfigureLogging(LogConfigUpdate) {
    //    }

    std::string DeviceConfiguration::getComponentType(std::string serviceName) {
        return _kernel.getConfig().find({"services", serviceName, "componentType"}).getName();
    }

    std::shared_ptr<config::Topics> DeviceConfiguration::getRunWithTopic() {
        return getTopics(configs.RUN_WITH_TOPIC);
    }

    config::Topic DeviceConfiguration::getRunWithDefaultPosixUser() {
        return getRunWithTopic()->lookup({configs.RUN_WITH_DEFAULT_POSIX_USER});
    }

    config::Topic DeviceConfiguration::getRunWithDefaultPosixShell() {
        return getRunWithTopic()->lookup({configs.RUN_WITH_DEFAULT_POSIX_SHELL});
    }

    config::Topic DeviceConfiguration::getRunWithDefaultWindowsUser() {
        return getRunWithTopic()->lookup({configs.RUN_WITH_DEFAULT_WINDOWS_USER});
    }

    std::shared_ptr<config::Topics> DeviceConfiguration::findRunWithDefaultSystemResourceLimits() {
        return _kernel.getConfig().findTopics(
            {"services",
             getNucleusComponentName(),
             "configuration",
             configs.RUN_WITH_TOPIC,
             "systemResourceLimits"}
        );
    }

    std::shared_ptr<config::Topics> DeviceConfiguration::getPlatformOverrideTopic() {
        return getTopics(configs.PLATFORM_OVERRIDE_TOPIC);
    }

    // Get thing name configuration. Also adds the thing name to the env vars if it has changed.
    config::Topic DeviceConfiguration::getThingName() {
        config::Topic thingNameTopic =
            _kernel.getConfig()
                .lookup({configs.SYSTEM_NAMESPACE_KEY, configs.DEVICE_PARAM_THING_NAME})
                .dflt("");

        // TODO: Greengrass Service
        _kernel.getConfig()
            .lookup({"setenv", configs.AWS_IOT_THING_NAME_ENV})
            .withValue(thingNameTopic.getName());

        return thingNameTopic;
    }

    config::Topic DeviceConfiguration::getCertificateFilePath() {
        // TODO: Add validator
        return _kernel.getConfig()
            .lookup({configs.SYSTEM_NAMESPACE_KEY, configs.DEVICE_PARAM_CERTIFICATE_FILE_PATH})
            .dflt("");
    }

    config::Topic DeviceConfiguration::getPrivateKeyFilePath() {
        // TODO: Add validator
        return _kernel.getConfig()
            .lookup({configs.SYSTEM_NAMESPACE_KEY, configs.DEVICE_PARAM_PRIVATE_KEY_PATH})
            .dflt("");
    }

    config::Topic DeviceConfiguration::getRootCAFilePath() {
        // TODO: Add validator
        return _kernel.getConfig()
            .lookup({configs.SYSTEM_NAMESPACE_KEY, configs.DEVICE_PARAM_ROOT_CA_PATH})
            .dflt("");
    }

    config::Topic DeviceConfiguration::getIpcSocketPath() {
        return _kernel.getConfig().find(
            {configs.SYSTEM_NAMESPACE_KEY, configs.DEVICE_PARAM_IPC_SOCKET_PATH}
        );
    }

    config::Topic DeviceConfiguration::getInterpolateComponentConfiguration() {
        return getTopic(configs.DEVICE_PARAM_INTERPOLATE_COMPONENT_CONFIGURATION).dflt(false);
    }

    config::Topic DeviceConfiguration::getGGDataEndpoint() {
        return getTopic(configs.DEVICE_PARAM_GG_DATA_ENDPOINT).dflt("");
    }

    config::Topic DeviceConfiguration::getIoTDataEndpoint() {
        return getTopic(configs.DEVICE_PARAM_IOT_DATA_ENDPOINT).dflt("");
    }

    config::Topic DeviceConfiguration::getIotCredentialEndpoint() {
        return getTopic(configs.DEVICE_PARAM_IOT_CRED_ENDPOINT).dflt("");
    }

    config::Topic DeviceConfiguration::getAWSRegion() {
        // TODO: Add validator
        return getTopic(configs.DEVICE_PARAM_AWS_REGION).dflt("");
    }

    config::Topic DeviceConfiguration::getFipsMode() {
        return getTopic(configs.DEVICE_PARAM_FIPS_MODE).dflt("false");
    }

    config::Topic DeviceConfiguration::getGreengrassDataPlanePort() {
        return getTopic(configs.DEVICE_PARAM_GG_DATA_PLANE_PORT).dflt(GG_DATA_PLANE_PORT_DEFAULT);
    }

    void DeviceConfiguration::setAwsRegion(std::string region) {
        // TODO: Add validator
        getTopic(configs.DEVICE_PARAM_AWS_REGION).withValue(region);
    }

    config::Topic DeviceConfiguration::getEnvironmentStage() {
        config::Timestamp modTime(1);
        return getTopic(configs.DEVICE_PARAM_ENV_STAGE)
            .withNewerValue(modTime, configs.DEFAULT_ENV_STAGE);
    }

    std::shared_ptr<config::Topics> DeviceConfiguration::getMQTTNamespace() {
        return getTopics(configs.DEVICE_MQTT_NAMESPACE);
    }

    std::shared_ptr<config::Topics> DeviceConfiguration::getSpoolerNamespace() {
        return getMQTTNamespace()->lookupTopics({configs.DEVICE_SPOOLER_NAMESPACE});
    }

    std::shared_ptr<config::Topics> DeviceConfiguration::getNetworkProxyNamespace() {
        return getTopics(configs.DEVICE_NETWORK_PROXY_NAMESPACE);
    }

    std::shared_ptr<config::Topics> DeviceConfiguration::getProxyNamespace() {
        return getNetworkProxyNamespace()->lookupTopics({configs.DEVICE_PROXY_NAMESPACE});
    }

    std::string DeviceConfiguration::getNoProxyAddresses() {
        config::Topic potentialTopic =
            getNetworkProxyNamespace()->lookup({configs.DEVICE_PARAM_NO_PROXY_ADDRESSES});
        return potentialTopic.getParent() ? "" : potentialTopic.getString();
    }

    std::string DeviceConfiguration::getProxyUrl() {
        config::Topic potentialTopic =
            getNetworkProxyNamespace()->find({configs.DEVICE_PARAM_PROXY_URL});
        return potentialTopic.getParent() ? "" : potentialTopic.getString();
    }

    std::string DeviceConfiguration::getProxyPassword() {
        config::Topic potentialTopic =
            getNetworkProxyNamespace()->find({configs.DEVICE_PARAM_PROXY_PASSWORD});
        return potentialTopic.getParent() ? "" : potentialTopic.getString();
    }

    config::Topic DeviceConfiguration::getIotRoleAlias() {
        return getTopic(configs.IOT_ROLE_ALIAS_TOPIC).dflt("");
    }

    config::Topic DeviceConfiguration::getComponentStoreMaxSizeBytes() {
        return getTopic(configs.COMPONENT_STORE_MAX_SIZE_BYTES);
    }

    config::Topic DeviceConfiguration::getDeploymentPollingFrequencySeconds() {
        return getTopic(configs.DEPLOYMENT_POLLING_FREQUENCY_SECONDS);
    }

    config::Topic DeviceConfiguration::gets3EndpointType() {
        return getTopic(configs.S3_ENDPOINT_TYPE).dflt("GLOBAL");
    }

    void DeviceConfiguration::validate() {
        validate(false);
    }

    void DeviceConfiguration::validate(bool cloudOnly) {
        std::string thingName = getThingName().getName();
        std::string certificateFilePath = getCertificateFilePath().getName();
        std::string privateKeyPath = getPrivateKeyFilePath().getName();
        std::string rootCAPath = getRootCAFilePath().getName();
        std::string iotDataEndpoint = getIoTDataEndpoint().getName();
        std::string iotCredEndpoint = getIotCredentialEndpoint().getName();
        std::string awsRegion = getAWSRegion().getName();

        validateDeviceConfiguration(
            thingName,
            certificateFilePath,
            privateKeyPath,
            rootCAPath,
            iotDataEndpoint,
            iotCredEndpoint,
            awsRegion,
            cloudOnly
        );
    }

    bool DeviceConfiguration::isDeviceConfiguredToTalkToCloud() {
        bool cachedValue = _deviceConfigValidationCachedResult.load();
        if(cachedValue) {
            return cachedValue;
        }
        // TODO: Add exception handling
        try {
            validate(true);
            _deviceConfigValidationCachedResult = true;
            return true;
        } catch(...) {
            _deviceConfigValidationCachedResult = false;
            return false;
        }
    }

    bool DeviceConfiguration::provisionInfoNodeChanged(
        config::ConfigNode node, bool checkThingNameOnly
    ) {
        // TODO: missing code
    }

    config::Topic DeviceConfiguration::getTopic(data::StringOrdInit parameterName) {
        return _kernel.getConfig().lookup(
            {"services", getNucleusComponentName(), "configuration", parameterName}
        );
    }

    std::shared_ptr<config::Topics> DeviceConfiguration::getTopics(data::StringOrdInit parameterName
    ) {
        return _kernel.getConfig().lookupTopics(
            {"services", getNucleusComponentName(), "configuration", parameterName}
        );
    }

    std::string DeviceConfiguration::getNucleusVersion() {
        std::string version;
        std::shared_ptr<config::Topics> componentTopic =
            _kernel.findServiceTopic(getNucleusComponentName());
        if(componentTopic && componentTopic->find({"version"}).getParent()) {
            version = componentTopic->find({"version"}).getName();
        }
        if(version.empty()) {
            return configs.FALLBACK_VERSION;
        } else {
            return version;
        }
    }

    std::string DeviceConfiguration::getVersionFromBuildRecipeFile() {
        // TODO: missing code
    }

    void DeviceConfiguration::validateDeviceConfiguration(
        std::string_view thingName,
        std::string_view certificateFilePath,
        std::string_view privateKeyPath,
        std::string_view rootCAPath,
        std::string_view iotDataEndpoint,
        std::string_view iotCredEndpoint,
        std::string_view awsRegion,
        bool cloudOnly
    ) {
        std::vector<std::string> errors;
        if(thingName.empty()) {
            errors.emplace_back(configs.DEVICE_PARAM_THING_NAME + configs.CANNOT_BE_EMPTY);
        }
        if(certificateFilePath.empty()) {
            errors.emplace_back(
                configs.DEVICE_PARAM_CERTIFICATE_FILE_PATH + configs.CANNOT_BE_EMPTY
            );
        }
        if(privateKeyPath.empty()) {
            errors.emplace_back(configs.DEVICE_PARAM_PRIVATE_KEY_PATH + configs.CANNOT_BE_EMPTY);
        }
        if(rootCAPath.empty()) {
            errors.emplace_back(configs.DEVICE_PARAM_ROOT_CA_PATH + configs.CANNOT_BE_EMPTY);
        }
        if(iotDataEndpoint.empty()) {
            errors.emplace_back(configs.DEVICE_PARAM_IOT_DATA_ENDPOINT + configs.CANNOT_BE_EMPTY);
        }
        if(iotCredEndpoint.empty()) {
            errors.emplace_back(configs.DEVICE_PARAM_IOT_CRED_ENDPOINT + configs.CANNOT_BE_EMPTY);
        }
        if(awsRegion.empty()) {
            errors.emplace_back(configs.DEVICE_PARAM_AWS_REGION + configs.CANNOT_BE_EMPTY);
        }
        try {
            validateEndpoints(awsRegion, iotCredEndpoint, iotDataEndpoint);
            if(!cloudOnly) {
                // TODO: Add platform
            }
        } catch(...) {
            // TODO: Add exception handling
        }
        if(!errors.empty()) {
        }
    }

    // Validate the IoT credential and data endpoint with the provided AWS region. Currently, it
    // checks that if the endpoints are provided, then the AWS region should be a part of the
    // URL.
    void DeviceConfiguration::validateEndpoints(
        std::string_view awsRegion,
        std::string_view iotCredEndpoint,
        std::string_view iotDataEndpoint
    ) {
        if(!awsRegion.empty()) {
            // TODO: missing code
        }
        // FIXME: implicit conversion
        if(!iotCredEndpoint.empty()
           && iotCredEndpoint.find(std::string{configs.AMAZON_DOMAIN_SEQUENCE})
                  != std::string_view::npos
           && iotCredEndpoint.find(awsRegion) == std::string::npos) {
            std::cerr << "IoT credential endpoint region does not match the AWS region of "
                         "the device";
        }
        if(!iotDataEndpoint.empty()
           && iotDataEndpoint.find(std::string{configs.AMAZON_DOMAIN_SEQUENCE})
                  != std::string_view::npos) {
            std::cerr << "IoT data endpoint region does not match the AWS region of the device";
        }
    }

    std::shared_ptr<config::Topics> DeviceConfiguration::getHttpClientOptions() {
        return getTopics("httpClient");
    }

} // namespace deployment
