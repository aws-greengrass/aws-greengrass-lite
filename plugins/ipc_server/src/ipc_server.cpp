#include "ipc_server.hpp"

IpcServer::IpcServer() noexcept {
    _authHandler = std::make_unique<AuthenticationHandler>();
}

void IpcServer::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    std::cerr << "[ipc-server] Running lifecycle phase " << phase.toString() << std::endl;
}

bool IpcServer::onBootstrap(ggapi::Struct structData) {
    structData.put(NAME, "aws.greengrass.ipc_server");
    return true;
}

bool IpcServer::onStart(ggapi::Struct data) {
    std::ignore = getScope().subscribeToTopic(
        keys.topicName, ggapi::TopicCallback::of(&IpcServer::cliHandler, this));
    auto system = _system.load();
    if(system.hasKey("ipcSocketPath")) {
        _socketPath = system.get<std::string>("ipcSocketPath");
    } else {
        std::filesystem::path filePath =
            std::filesystem::canonical(system.getValue<std::string>({"rootPath"})) / SOCKET_NAME;
        _socketPath = filePath.string();
    }
    _listener = std::make_shared<ServerListener>();
    try {
        _listener->Connect(_socketPath);
    } catch(std::runtime_error &e) {
        throw ggapi::GgApiError(e.what());
    }
    return true;
}

ggapi::Struct IpcServer::cliHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct req) {
    auto serviceName = req.getValue<std::string>({keys.serviceName});

    auto resp = ggapi::Struct::create();
    resp.put(keys.socketPath, _socketPath);
    resp.put(keys.cliAuthToken, _authHandler->generateAuthToken(serviceName));
    return resp;
}

bool IpcServer::onTerminate(ggapi::Struct structData) {
    _listener->Disconnect();
    return true;
}

bool IpcServer::onBind(ggapi::Struct data) {
    _system = getScope().anchor(data.getValue<ggapi::Struct>({"system"}));
    _config = getScope().anchor(data.getValue<ggapi::Struct>({"config"}));
    _configRoot = getScope().anchor(data.getValue<ggapi::Struct>({"configRoot"}));
    return true;
}
