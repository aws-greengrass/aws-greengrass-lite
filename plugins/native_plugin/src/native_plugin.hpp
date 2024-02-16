#pragma once

#include "plugin.hpp"

#include "abstract_process_manager.hpp"

struct Keys {
    ggapi::Symbol infoTopicName{"aws.greengrass.RequestIpcInfo"};
    ggapi::Symbol serviceName{"serviceName"};
    ggapi::Symbol startProcessTopic{"aws.greengrass.Native.StartProcess"};
    ggapi::Symbol socketPath{"domain_socket_path"};
    ggapi::Symbol cliAuthToken{"cli_auth_token"};
    ggapi::Symbol thingName{"thingName"};
};

inline const Keys keys;

class NativePlugin : public ggapi::Plugin {
    std::optional<std::string> _user;
    std::optional<std::string> _group;
    std::string _shell;
    std::atomic<ggapi::Struct> _system;
    std::atomic<ggapi::Struct> _nucleus;
    std::string _authToken;
    std::string _socketPath;
    std::string _thingName;
    static constexpr auto SERVICE_NAME = "aws.greengrass.Native";

    ipc::ProcessManager _manager;

public:
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;
    bool onBind(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
    bool onBootstrap(ggapi::Struct structData) override;
    bool onRun(ggapi::Struct data) override;

    ggapi::Struct startProcessListener(
        ggapi::Task task, ggapi::Symbol topic, ggapi::Struct callData);

    static NativePlugin &get() {
        static NativePlugin instance{};
        return instance;
    }
};
