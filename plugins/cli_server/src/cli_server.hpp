#pragma once
#include "plugin.hpp"
#include <algorithm>
#include <filesystem>
#include <random>
#include <unordered_map>

struct Keys {
    ggapi::Symbol serviceName{"aws.greengrass.Cli"};
};

class CliServer final : public ggapi::Plugin {
    static constexpr std::string_view IPC_SOCKET_PATH = "/tmp/gglite-ipc.socket";
    static constexpr std::string_view CLI_AUTH_TOKEN = "cli_auth_token";
    static constexpr std::string_view DOMAIN_SOCKET_PATH = "domain_socket_path";
    static const inline std::string CLI_IPC_INFO_FILE_PATH = "info.json";
    // TODO: Should come from nucleus kernel?
    static constexpr std::string_view CLI_IPC_INFO_PATH{"cli_ipc_info"};

    std::unordered_map<std::string, std::string> _clientIdToAuthToken;

    static std::string generateCliToken();

    void generateCliIpcInfo(const std::filesystem::path &);

public:
    CliServer() = default;
    bool onBootstrap(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
    bool onRun(ggapi::Struct data) override;
    bool onTerminate(ggapi::Struct data) override;
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;
    static CliServer &get() {
        static CliServer instance{};
        return instance;
    }
};
