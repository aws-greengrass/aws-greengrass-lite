#include <ipc_server.hpp>

static constexpr int SEED = 123;

// Initializes global CRT API
// TODO: What happens when multiple plugins use the CRT?
static const Aws::Crt::ApiHandle apiHandle{};

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
    std::filesystem::path rootPath = system.getValue<std::string>({"rootPath"});
    auto socketPath = rootPath / SOCKET_NAME;
    _listener = std::make_shared<ServerListener>();
    try {
        _listener->Connect(socketPath.string());
    } catch(std::runtime_error &e) {
        throw ggapi::GgApiError(e.what());
    }
    return true;
}

std::string IpcServer::generateIpcToken() {
    // SECURITY-TODO: Make random generation secure
    static constexpr size_t TOKEN_LENGTH = 16;
    std::string_view chars = "0123456789"
                             "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    thread_local std::mt19937 rng{SEED};
    auto dist = std::uniform_int_distribution{{}, chars.size() - 1};
    auto result = std::string(TOKEN_LENGTH, '\0');
    std::generate_n(result.begin(), TOKEN_LENGTH, [&]() { return chars[dist(rng)]; });
    return result;
}

ggapi::Struct IpcServer::cliHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct) {
    auto system = _system.load();
    std::filesystem::path rootPath =
        std::filesystem::canonical(system.getValue<std::string>({"rootPath"}));
    auto socketPath = rootPath / SOCKET_NAME;

    auto resp = ggapi::Struct::create();
    resp.put(keys.socketPath, socketPath.string());
    resp.put(keys.cliAuthToken, generateIpcToken());
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
