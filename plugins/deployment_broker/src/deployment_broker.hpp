#pragma once
#include "plugin.hpp"

struct Keys {
    ggapi::Symbol deploymentStatusTopicName{"aws.greengrass.deployment.Status"};
    ggapi::Symbol recipeRetrieveTopicName{"aws.greengrass.deployment.Recipe"};
    ggapi::Symbol artifactsRetrieveTopicName{"aws.greengrass.deployment.Artifact"};
    ggapi::Symbol dataRetrieveTopicName{"aws.greengrass.deployment.Data"};
    ggapi::Symbol executeTopicName{"aws.greengrass.environment.Execute"};
    ggapi::Symbol serviceName{"aws.greengrass.deployment_broker"};
};

class DeploymentBroker final : public ggapi::Plugin {

    ggapi::Struct statusDeploymentHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct);
    std::atomic<ggapi::Struct> _system;
    std::atomic<ggapi::Struct> _config;
    std::atomic<ggapi::Struct> _configRoot;

public:
    DeploymentBroker() = default;
    bool onBootstrap(ggapi::Struct data) override;
    bool onBind(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
    bool onRun(ggapi::Struct data) override;
    bool onTerminate(ggapi::Struct data) override;
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;
    static DeploymentBroker &get() {
        static DeploymentBroker instance{};
        return instance;
    }
};
