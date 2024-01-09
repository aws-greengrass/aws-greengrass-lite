#include "cli_server.hpp"
#include <fstream>

static const Keys keys;

static constexpr int SEED = 123;

void CliServer::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    ggapi::Symbol phaseOrd{phase};
    std::cerr << "[cli-server] Running lifecycle phase " << phaseOrd.toString() << std::endl;
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
    static constexpr size_t TOKEN_LENGTH = 16;
    std::string_view chars = "0123456789"
                             "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    thread_local std::mt19937 rng(SEED);
    auto dist = std::uniform_int_distribution{{}, chars.size() - 1};
    auto result = std::string(TOKEN_LENGTH, '\0');
    std::generate_n(result.begin(), TOKEN_LENGTH, [&]() { return chars[dist(rng)]; });
    return result;
}

void CliServer::generateCliIpcInfo(const std::filesystem::path &ipcCliInfoPath) {
    // TODO: Revoke outdated tokens
    // clean up outdated tokens
    for(const auto &entry : std::filesystem::directory_iterator(ipcCliInfoPath)) {
        std::filesystem::remove_all(entry.path());
    }

    auto clientId = CLI_IPC_INFO_FILE_PATH;
    if(_clientIdToAuthToken.find(clientId) != _clientIdToAuthToken.end()) {
        // duplicate entry
        return;
    }

    // generate token
    auto cliAuthToken = generateCliToken();

    _clientIdToAuthToken.insert({clientId, cliAuthToken});

    ggapi::Struct ipcInfo = ggapi::Struct::create();
    ipcInfo.put(CLI_AUTH_TOKEN, cliAuthToken);
    ipcInfo.put(
        DOMAIN_SOCKET_PATH,
        IPC_SOCKET_PATH); // TODO: override socket path from recipe or nucleus config

    // write to path
    auto filePath = ipcCliInfoPath / clientId;
    std::ofstream ofstream(filePath);
    ggapi::Buffer buffer = ipcInfo.toJson();
    buffer.write(ofstream);
    ofstream.flush();
    ofstream.close();

    // set file permissions
    std::filesystem::permissions(
        filePath, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}

bool CliServer::onRun(ggapi::Struct data) {
    // GG-Interop: Load from the system instead of service
    auto system = data.getValue<ggapi::Struct>({"system"});
    std::filesystem::path rootPath = system.getValue<std::string>({"rootpath"});
    generateCliIpcInfo(rootPath / CLI_IPC_INFO_PATH);
    return true;
}

bool CliServer::onTerminate(ggapi::Struct data) {
    return true;
}
