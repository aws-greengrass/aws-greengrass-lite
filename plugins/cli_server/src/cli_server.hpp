#pragma once
#include "plugin.hpp"
#include <filesystem>
#include <random>
#include <sstream>
#include <unordered_map>

struct Keys {
    ggapi::Symbol infoTopicName{"aws.greengrass.RequestIpcInfo"};
    ggapi::Symbol createDeploymentTopicName{"aws.greengrass.deployment.Offer"};
    ggapi::Symbol cancelDeploymentTopicName{"aws.greengrass.deployment.Cancel"};
    ggapi::Symbol createLocalDeployment{"IPC::aws.greengrass#CreateLocalDeployment"};
    ggapi::Symbol cancelLocalDeployment{"IPC::aws.greengrass#CancelLocalDeployment"};
    ggapi::Symbol getLocalDeploymentStatus{"IPC::aws.greengrass#GetLocalDeploymentStatus"};
    ggapi::Symbol listLocalDeployments{"IPC::aws.greengrass#ListLocalDeployments"};
    ggapi::Symbol serviceName{"serviceName"};
    ggapi::Symbol socketPath{"domain_socket_path"};
    ggapi::Symbol cliAuthToken{"cli_auth_token"};
    ggapi::Symbol shape{"shape"};
    ggapi::Symbol channel{"channel"};
    ggapi::Symbol errorCode{"errorCode"};
    ggapi::Symbol terminate{"terminate"};
};

struct DeploymentKeys {
    ggapi::Symbol deploymentDocumentobj{"deploymentDocumentobj"};
    ggapi::Symbol deploymentDocument{"deploymentDocument"};
    ggapi::Symbol requestId{"requestId"};
    ggapi::Symbol requestTimestamp{"requestTimestamp"};
    ggapi::Symbol componentsToMerge{"componentsToMerge"};
    ggapi::Symbol componentsToRemove{"componentsToRemove"};
    ggapi::Symbol groupName{"groupName"};
    ggapi::Symbol requestCapabilities{"requestCapabilities"};
    ggapi::Symbol componentNameToConfig{"componentNameToConfig"};
    ggapi::Symbol configurationUpdate{"configurationUpdate"};
    ggapi::Symbol componentToRunWithInfo{"componentToRunWithInfo"};
    ggapi::Symbol recipeDirectoryPath{"recipeDirectoryPath"};
    ggapi::Symbol artifactsDirectoryPath{"artifactsDirectoryPath"};
    ggapi::Symbol failureHandlingPolicy{"failureHandlingPolicy"};
    ggapi::Symbol deploymentType{"deploymentType"};
    ggapi::Symbol id{"id"};
    ggapi::Symbol isCancelled{"isCancelled"};
    ggapi::Symbol deploymentStage{"deploymentStage"};
    ggapi::Symbol stageDetails{"stageDetails"};
    ggapi::Symbol errorStack{"errorStack"};
    ggapi::Symbol errorTypes{"errorTypes"};
};

using DeploymentHandler = std::function<ggapi::Struct(ggapi::Struct packet)>;

class RandomUUID {
    std::mt19937 _rng{};
    static inline auto _dist = std::uniform_int_distribution<uint32_t>(0, 15);

public:
    std::string operator()() {
        // TODO: Generate real uuid
        auto generateRandom = [this](auto &&n) {
            std::ostringstream ss;
            for(auto i = 0; i < n; i++) {
                ss << std::hex << _dist(_rng);
            }
            return ss.str();
        };
        std::string uuid;
        uuid += generateRandom(8) + "-";
        uuid += generateRandom(4) + "-";
        uuid += generateRandom(4) + "-";
        uuid += generateRandom(4) + "-";
        uuid += generateRandom(12);
        return uuid;
    }
};

class CliServer final : public ggapi::Plugin {
    static const inline std::string CLI_IPC_INFO_FILE_PATH = "info.json";
    // TODO: Get the path from host-environment plugin
    static constexpr std::string_view CLI_IPC_INFO_PATH{"cli_ipc_info"};
    static constexpr std::string_view SERVICE_NAME{"aws.greengrass.Cli"};

    std::unordered_map<std::string, std::string> _clientIdToAuthToken;
    void generateCliIpcInfo(const std::filesystem::path &);

    std::atomic<ggapi::Struct> _system;
    std::atomic<ggapi::Struct> _config;
    std::atomic<ggapi::Struct> _configRoot;

    ggapi::Struct createLocalDeploymentHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct);
    ggapi::Struct cancelLocalDeploymentHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct);
    ggapi::Struct getLocalDeploymentStatusHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct);
    ggapi::Struct listLocalDeploymentsHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct);

    std::vector<std::tuple<ggapi::Symbol, ggapi::Channel, DeploymentHandler>> _subscriptions;
    std::shared_mutex _subscriptionMutex;

    RandomUUID _randomUUID{};

public:
    CliServer() = default;
    bool onBootstrap(ggapi::Struct data) override;
    bool onBind(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
    bool onRun(ggapi::Struct data) override;
    bool onTerminate(ggapi::Struct data) override;
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;
    static CliServer &get() {
        static CliServer instance{};
        return instance;
    }
};
