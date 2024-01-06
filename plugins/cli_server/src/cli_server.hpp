#pragma once
#include "plugin.hpp"
#include <algorithm>
#include <filesystem>
#include <random>
#include <unordered_map>

#if defined(__unix__)
#include "unix_platform.hpp"
#elif defined(_WIN32)
#include "windows_platform.hpp"
#elif defined(__APPLE__)
#include "darwin_platform.hpp"
#endif

struct Keys {
    ggapi::Symbol serviceName{"aws.greengrass.Cli"};
};

class CliServer final : public ggapi::Plugin {
    static constexpr std::string_view IPC_SOCKET_PATH = "/tmp/gglite-ipc.socket";
    static constexpr std::string_view CLI_AUTH_TOKEN = "cli_auth_token";
    static constexpr std::string_view DOMAIN_SOCKET_PATH = "domain_socket_path";
    static const inline std::string USER_CLIENT_ID_PREFIX = "user-";
    static const inline std::string GROUP_CLIENT_ID_PREFIX = "group-";
    static const inline std::string GG_CLI_CLIENT_ID_PREFIX = "greengrass-cli#";
    static const inline std::string POLICY_NAME_PREFIX = "aws.greengrass.Cli:pubsub:";
    // TODO: Should come from nucleus kernel?
    static constexpr std::string_view CLI_IPC_INFO_PATH_NAME{"cli_ipc_info"};

    std::unordered_map<std::string, std::string> _clientIdToAuthToken;

    static std::string generateCliToken();

    template<typename T, typename std::enable_if_t<std::is_base_of_v<Platform, T>, bool> = true>
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
