#pragma once

#include "plugin.hpp"
#include "rpc_server.hpp"
#include "server_connection.hpp"
#include "server_listener.hpp"

#include <aws/crt/Api.h>

struct Keys {
    ggapi::Symbol topicName{"aws.greengrass.RunIpcServer"};
    ggapi::Symbol serviceName{"aws.greengrass.IPC"};

    static const Keys &get() {
        static Keys keys{};
        return keys;
    }
};

class IpcPlugin : public ggapi::Plugin {
    static const Keys keys;

public:
    IpcPlugin() = default;
    void beforeLifecycle(ggapi::StringOrd phase, ggapi::Struct data) override;
    bool onBootstrap(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
    bool onRun(ggapi::Struct data) override;
    static ggapi::Struct someListener(
        ggapi::Task task, ggapi::StringOrd topic, ggapi::Struct callData);
    static IpcPlugin &get() {
        static IpcPlugin instance;
        return instance;
    }
};
