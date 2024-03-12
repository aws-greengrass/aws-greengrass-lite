#include "cli_server.hpp"

#include <algorithm>
#include <fstream>
#include <tuple>

static const Keys keys;

static const DeploymentKeys deploymentKeys;

bool CliServer::onInitialize(ggapi::Struct data) {
    data.put(NAME, keys.serviceName);
    _system = getScope().anchor(data.getValue<ggapi::Struct>({"system"}));
    _config = getScope().anchor(data.getValue<ggapi::Struct>({"config"}));
    _configRoot = getScope().anchor(data.getValue<ggapi::Struct>({"configRoot"}));
    return true;
}

bool CliServer::onStart(ggapi::Struct data) {
    std::ignore = getScope().subscribeToTopic(
        keys.createLocalDeployment,
        ggapi::TopicCallback::of(&CliServer::createLocalDeploymentHandler, this));
    std::ignore = getScope().subscribeToTopic(
        keys.cancelLocalDeployment,
        ggapi::TopicCallback::of(&CliServer::cancelLocalDeploymentHandler, this));
    std::ignore = getScope().subscribeToTopic(
        keys.getLocalDeploymentStatus,
        ggapi::TopicCallback::of(&CliServer::getLocalDeploymentStatusHandler, this));
    std::ignore = getScope().subscribeToTopic(
        keys.listLocalDeployments,
        ggapi::TopicCallback::of(&CliServer::listLocalDeploymentsHandler, this));
    std::ignore = getScope().subscribeToTopic(
        keys.listLocalDeployments,
        ggapi::TopicCallback::of(&CliServer::listDeploymentsHandler, this));
    // GG-Interop: Extract rootpath from system config
    auto system = _system.load();
    std::filesystem::path rootPath = system.getValue<std::string>({"rootpath"});
    generateCliIpcInfo(rootPath / CLI_IPC_INFO_PATH);
    return true;
}

void CliServer::generateCliIpcInfo(const std::filesystem::path &ipcCliInfoPath) {
    // TODO: Revoke outdated tokens
    // clean up outdated tokens
    for(const auto &entry : std::filesystem::directory_iterator(ipcCliInfoPath)) {
        std::filesystem::remove_all(entry.path());
    }

    const auto &clientId = CLI_IPC_INFO_FILE_PATH;
    if(_clientIdToAuthToken.find(clientId) != _clientIdToAuthToken.cend()) {
        // duplicate entry
        return;
    }

    // get ipc info
    auto request = ggapi::Struct::create();
    request.put(keys.serviceName, SERVICE_NAME);
    auto result = ggapi::Task::sendToTopic(keys.infoTopicName, request);

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

bool CliServer::onStop(ggapi::Struct data) {
    return true;
}

bool CliServer::onError_stop(ggapi::Struct data) {
    return true;
}

ggapi::Struct CliServer::createLocalDeploymentHandler(
    ggapi::Task, ggapi::Symbol, ggapi::Struct request) {
    auto deploymentDocument = request;
    auto deploymentId = _randomUUID();
    auto now = std::chrono::system_clock::now();
    auto milli =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    deploymentDocument.put(deploymentKeys.requestId, deploymentId);
    deploymentDocument.put(deploymentKeys.requestTimestamp, milli);

    auto deploymentJson = deploymentDocument.toJson();
    auto deploymentVec = deploymentJson.get<std::vector<uint8_t>>(0, deploymentJson.size());
    auto deploymentString = std::string{deploymentVec.begin(), deploymentVec.end()};

    auto deployment = ggapi::Struct::create();
    // TODO: Fill in other key values
    // TODO: remove deployment document obj
    deployment.put(deploymentKeys.deploymentDocumentobj, 0);
    deployment.put(deploymentKeys.deploymentDocument, deploymentString);
    deployment.put(deploymentKeys.deploymentType, "LOCAL");
    deployment.put(deploymentKeys.id, deploymentId);
    deployment.put(deploymentKeys.isCancelled, false);
    deployment.put(deploymentKeys.deploymentStage, "DEFAULT");
    deployment.put(deploymentKeys.stageDetails, 0);
    deployment.put(deploymentKeys.errorStack, 0);
    deployment.put(deploymentKeys.errorTypes, 0);

    auto channel = getScope().anchor(ggapi::Channel::create());
    _subscriptions.emplace_back(deploymentId, channel, [](ggapi::Struct req) { return req; });
    channel.addCloseCallback([this, channel]() {
        std::unique_lock lock(_subscriptionMutex);
        auto it = std::find_if(_subscriptions.begin(), _subscriptions.end(), [channel](auto sub) {
            return std::get<1>(sub) == channel;
        });
        std::iter_swap(it, std::prev(_subscriptions.end()));
        _subscriptions.pop_back();
        channel.release();
    });
    auto result = ggapi::Task::sendToTopic(keys.createDeploymentTopicName, deployment);
    if(result.getValue<bool>({"status"})) {
        auto message = ggapi::Struct::create();
        message.put("deploymentId", deploymentId);
        return ggapi::Struct::create().put(keys.channel, channel).put(keys.shape, message);
    } else {
        return ggapi::Struct::create().put(keys.errorCode, 1);
    }
}

ggapi::Struct CliServer::listDeploymentsHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct request) {
    auto deploymentDocument = request;
    auto requestId = _randomUUID();
    deploymentDocument.put(deploymentKeys.requestId, requestId);
    auto deploymentJson = deploymentDocument.toJson();
    auto deploymentVec = deploymentJson.get<std::vector<uint8_t>>(0, deploymentJson.size());
    auto deploymentString = std::string{deploymentVec.begin(), deploymentVec.end()};

    auto deployment = ggapi::Struct::create();
    deployment.put(deploymentKeys.deploymentDocumentobj, 0);
    deployment.put(deploymentKeys.deploymentDocument, deploymentString);
    deployment.put(deploymentKeys.deploymentType, "LOCAL");
    deployment.put(deploymentKeys.id, requestId);
    deployment.put(deploymentKeys.isCancelled, false);
    deployment.put(deploymentKeys.deploymentStage, "DEFAULT");
    deployment.put(deploymentKeys.stageDetails, 0);
    deployment.put(deploymentKeys.errorStack, 0);
    deployment.put(deploymentKeys.errorTypes, 0);

    // TODO: Remove channel
    auto channel = getScope().anchor(ggapi::Channel::create());
    auto result = ggapi::Task::sendToTopic(keys.createDeploymentTopicName, deployment);
    if(result.getValue<bool>({"status"})) {
        auto message = ggapi::Struct::create();
        message.put("deploymentId", requestId);
        return ggapi::Struct::create().put(keys.channel, channel).put(keys.shape, message);
    } else {
        return ggapi::Struct::create().put(keys.errorCode, 1);
    }
}

ggapi::Struct CliServer::cancelLocalDeploymentHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct) {
    // TODO: Not implemented
    std::ignore = ggapi::Task::sendToTopic(keys.cancelDeploymentTopicName, ggapi::Struct::create());
    return ggapi::Struct::create();
}

ggapi::Struct CliServer::getLocalDeploymentStatusHandler(
    ggapi::Task, ggapi::Symbol, ggapi::Struct) {
    // TODO: Not implemented
    return ggapi::Struct::create();
}

ggapi::Struct CliServer::listLocalDeploymentsHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct) {
    // TODO: Not implemented
    return ggapi::Struct::create();
}
