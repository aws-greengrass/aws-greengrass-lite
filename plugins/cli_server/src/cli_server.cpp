#include "cli_server.hpp"
#include <fstream>

static const Keys keys;

void CliServer::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    ggapi::Symbol phaseOrd{phase};
    std::cerr << "[cli] Running lifecycle phase " << phaseOrd.toString() << std::endl;
}

bool CliServer::onBootstrap(ggapi::Struct data) {
    data.put(NAME, keys.serviceName);
    return true;
}

bool CliServer::onStart(ggapi::Struct data) {
    return true;
}

std::string CliServer::generateCliToken() {
    // TODO: authentication handler from IPC to generate token
    size_t len = 16;
    std::string_view chars = "0123456789"
                             "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    thread_local std::mt19937 rng(123);
    auto dist = std::uniform_int_distribution{{}, chars.size() - 1};
    auto result = std::string(len, '\0');
    std::generate_n(result.begin(), len, [&]() { return chars[dist(rng)]; });
    return result;
}

template<typename T>
void CliServer::generateCliIpcInfo(const std::filesystem::path &ipcCliInfoPath) {
    std::unique_ptr<T> platform = std::make_unique<T>();
    // TODO: Revoke outdated tokens
    // clean up outdated tokens
    for(const auto &entry : std::filesystem::directory_iterator(ipcCliInfoPath)) {
        std::filesystem::remove_all(entry.path());
    }

    auto clientId = USER_CLIENT_ID_PREFIX + platform->lookupCurrentUser().principalIdentifier;
    std::string_view serviceName = GG_CLI_CLIENT_ID_PREFIX + clientId;
    if(_clientIdToAuthToken.find(clientId) != _clientIdToAuthToken.end()) {
        // duplicate entry
        return;
    }

    // TODO: authorize pub/sub for cli
    // generate token
    std::string cliAuthToken = generateCliToken();

    _clientIdToAuthToken.insert({clientId, cliAuthToken});

    ggapi::Struct ipcInfo = ggapi::Struct::create();
    ipcInfo.put(CLI_AUTH_TOKEN, cliAuthToken);
    ipcInfo.put(
        DOMAIN_SOCKET_PATH,
        IPC_SOCKET_PATH); // TODO: override socket path from recipe or nucleus config

    // write to path
    auto filePath = ipcCliInfoPath / clientId;
    std::ofstream ostrm(filePath);
    ggapi::Buffer buffer = ipcInfo.toJson();
    const auto jsonInfo = buffer.get<std::vector<char>>(0, buffer.size());
    ostrm.write(jsonInfo.data(), jsonInfo.size());
    ostrm.flush();
    // set file permissions
    std::filesystem::permissions(
        filePath, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}

bool CliServer::onRun(ggapi::Struct data) {
    // GG-Interop: Load from the system instead of service
    auto system = data.getValue<ggapi::Struct>({"system"});
    std::filesystem::path rootPath = system.getValue<std::string>({"rootpath"});
#if defined(__unix__)
    generateCliIpcInfo<UnixPlatform>(rootPath / CLI_IPC_INFO_PATH_NAME);
#else
    std::cerr << "Unsupported OS";
#endif
    return true;
}

bool CliServer::onTerminate(ggapi::Struct data) {
    return true;
}
