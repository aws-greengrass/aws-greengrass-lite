#include "ipc_server.hpp"

IpcServer::IpcServer() noexcept {
    _authHandler = std::make_unique<AuthenticationHandler>();
}

bool IpcServer::onInitialize(ggapi::Struct data) {
    std::ignore = util::getDeviceSdkApiHandle(); // Make sure Api initialized
    data.put(NAME, "aws.greengrass.ipc_server");
    std::unique_lock guard{_mutex};
    _system = data.getValue<ggapi::Struct>({"system"});
    _config = data.getValue<ggapi::Struct>({"config"});
    return true;
}

bool IpcServer::onStart(ggapi::Struct data) {
    std::unique_lock guard{_mutex};
    _ipcInfoSubs = ggapi::Subscription::subscribeToTopic(
        keys.requestIpcInfoTopic, ggapi::TopicCallback::of(&IpcServer::cliHandler, this));
    auto system = _system;
    if(system.hasKey("ipcSocketPath")) {
        _socketPath = system.get<std::string>("ipcSocketPath");
    } else {
        std::filesystem::path filePath =
            std::filesystem::canonical(system.getValue<std::string>({"rootPath"})) / SOCKET_NAME;
        _socketPath = filePath.string();
    }
    _listener = std::make_shared<ServerListener>(getModule());
    try {
        // TODO: Make non-blocking
        _listener->Connect(_socketPath);
    } catch(std::runtime_error &e) {
        throw ggapi::GgApiError(e.what());
    }
    return true;
}

// TODO: Better name for this?
ggapi::ObjHandle IpcServer::cliHandler(ggapi::Symbol, const ggapi::Container &reqBase) {
    ggapi::Struct req{reqBase};
    auto serviceName = req.getValue<std::string>({keys.serviceName});

    std::shared_lock guard{_mutex};
    auto resp = ggapi::Struct::create();
    resp.put(keys.socketPath, _socketPath);
    resp.put(keys.cliAuthToken, _authHandler->generateAuthToken(serviceName));
    return resp;
}

bool IpcServer::onStop(ggapi::Struct structData) {
    std::shared_lock guard{_mutex};
    _listener->Disconnect();
    return true;
}
