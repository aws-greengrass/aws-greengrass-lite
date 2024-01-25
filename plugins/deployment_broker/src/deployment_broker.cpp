#include "deployment_broker.hpp"

static const Keys keys;

void DeploymentBroker::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    ggapi::Symbol phaseOrd{phase};
    std::cerr << "[deployment-broker] Running lifecycle phase " << phaseOrd.toString() << std::endl;
}

bool DeploymentBroker::onBootstrap(ggapi::Struct data) {
    data.put(NAME, keys.serviceName);
    return true;
}

bool DeploymentBroker::onBind(ggapi::Struct data) {
    _system = getScope().anchor(data.getValue<ggapi::Struct>({"system"}));
    _config = getScope().anchor(data.getValue<ggapi::Struct>({"config"}));
    _configRoot = getScope().anchor(data.getValue<ggapi::Struct>({"configRoot"}));
    return true;
}

bool DeploymentBroker::onStart(ggapi::Struct data) {
    std::ignore = getScope().subscribeToTopic(
        keys.deploymentStatusTopicName,
        ggapi::TopicCallback::of(&DeploymentBroker::statusDeploymentHandler, this));
    return true;
}

bool DeploymentBroker::onRun(ggapi::Struct data) {
    return true;
}

bool DeploymentBroker::onTerminate(ggapi::Struct data) {
    return true;
}

ggapi::Struct DeploymentBroker::statusDeploymentHandler(
    ggapi::Task, ggapi::Symbol, ggapi::Struct request) {
    auto recipeDir = request.get<std::string>("");
    return ggapi::Struct::create();
}
