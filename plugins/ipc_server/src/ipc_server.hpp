#pragma once

#include <aws/crt/Api.h>

#include "authentication_handler.hpp"
#include "cpp_api.hpp"
#include "server_listener.hpp"
#include <plugin.hpp>

struct Keys {
private:
    Keys() = default;

public:
    ggapi::Symbol terminate{"terminate"};
    ggapi::Symbol contentType{"contentType"};
    ggapi::Symbol serviceModelType{"serviceModelType"};
    ggapi::Symbol shape{"shape"};
    ggapi::Symbol accepted{"accepted"};
    ggapi::Symbol errorCode{"errorCode"};
    ggapi::Symbol channel{"channel"};
    ggapi::Symbol socketPath{"domain_socket_path"};
    ggapi::Symbol cliAuthToken{"cli_auth_token"};
    ggapi::Symbol requestIpcInfoTopic{"aws.greengrass.RequestIpcInfo"};
    ggapi::Symbol serviceName{"serviceName"};
    static const Keys &get() {
        static Keys keys;
        return keys;
    }
};

static const auto &keys = Keys::get();

class IpcServer final : public ggapi::Plugin {
private:
    // TODO: This needs to come from host-environment plugin
    static constexpr std::string_view SOCKET_NAME = "gglite-ipc.socket";

    ggapi::ObjHandle cliHandler(ggapi::Symbol, const ggapi::Container &);

    ggapi::Struct _system;
    ggapi::Struct _config;
    ggapi::Subscription _ipcInfoSubs;

    std::string _socketPath;

    std::unique_ptr<AuthenticationHandler> _authHandler;

public:
    IpcServer() noexcept;
    bool onBootstrap(ggapi::Struct data) override;
    bool onBind(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
    bool onTerminate(ggapi::Struct data) override;
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;

    static IpcServer &get() {
        static IpcServer instance{};
        return instance;
    }

private:
    std::shared_ptr<ServerListener> _listener;
};
