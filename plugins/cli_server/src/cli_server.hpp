#pragma once
#include "plugin.hpp"
#include <filesystem>
#include <unordered_map>

struct Keys {
    ggapi::Symbol topicName{"aws.greengrass.RequestIpcInfo"};
    ggapi::Symbol serviceName{"aws.greengrass.Cli"};
    ggapi::Symbol socketPath{"domain_socket_path"};
    ggapi::Symbol cliAuthToken{"cli_auth_token"};
};

class CliServer final : public ggapi::Plugin {
    static const inline std::string CLI_IPC_INFO_FILE_PATH = "info.json";
    // TODO: Get the path from host-environment plugin
    static constexpr std::string_view CLI_IPC_INFO_PATH{"cli_ipc_info"};

    std::unordered_map<std::string, std::string> _clientIdToAuthToken;
    void generateCliIpcInfo(const std::filesystem::path &);

    std::atomic<ggapi::Struct> _system;
    std::atomic<ggapi::Struct> _config;
    std::atomic<ggapi::Struct> _configRoot;

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
