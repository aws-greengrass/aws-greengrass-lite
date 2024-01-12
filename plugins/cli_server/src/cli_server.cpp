#include "cli_server.hpp"

#include <algorithm>
#include <fstream>

static const Keys keys;

void CliServer::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    ggapi::Symbol phaseOrd{phase};
    std::cerr << "[cli-server] Running lifecycle phase " << phaseOrd.toString() << std::endl;
}

bool CliServer::onBootstrap(ggapi::Struct data) {
    data.put(NAME, keys.serviceName);
    return true;
}

bool CliServer::onBind(ggapi::Struct data) {
    _system = getScope().anchor(data.getValue<ggapi::Struct>({"system"}));
    _config = getScope().anchor(data.getValue<ggapi::Struct>({"config"}));
    _configRoot = getScope().anchor(data.getValue<ggapi::Struct>({"configRoot"}));
    return true;
}

bool CliServer::onStart(ggapi::Struct data) {
    return true;
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

    // get ipc info
    auto result = ggapi::Task::sendToTopic(keys.topicName, ggapi::Struct::create());

    auto socketPath = result.get<std::string>(keys.socketPath);
    auto cliAuthToken = result.get<std::string>(keys.cliAuthToken);

    _clientIdToAuthToken.insert({clientId, cliAuthToken});

    ggapi::Struct ipcInfo = ggapi::Struct::create();
    ipcInfo.put(keys.cliAuthToken, cliAuthToken);
    ipcInfo.put(
        keys.socketPath,
        socketPath); // TODO: override socket path from recipe or nucleus config

    // write to the path
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
    // GG-Interop: Extract rootpath from system config
    auto system = _system.load();
    std::filesystem::path rootPath = system.getValue<std::string>({"rootpath"});
    generateCliIpcInfo(rootPath / CLI_IPC_INFO_PATH);
    return true;
}

bool CliServer::onTerminate(ggapi::Struct data) {
    return true;
}
