#include "cli_server.hpp"
#include <interfaces/ipc_auth_info.hpp>
#include <ipc_standard_errors.hpp>

#include <algorithm>
#include <fstream>
#include <tuple>
#include <utility>

static const Keys keys;

static const DeploymentKeys deploymentKeys;

void CliServer::onInitialize(ggapi::Struct data) {
    data.put(NAME, "aws.greengrass.cli_server"); // TODO: This should come from recipe
    std::unique_lock guard{_mutex};
    _system = data.getValue<ggapi::Struct>({"system"});
    _config = data.getValue<ggapi::Struct>({"config"});
}

void CliServer::onStart(ggapi::Struct data) {
    std::shared_lock guard{_mutex};
    // TODO: also need to call close() onStop
    _createLocalDeploymentSubs = ggapi::Subscription::subscribeToTopic(
        keys.createLocalDeployment,
        ggapi::TopicCallback::of(&CliServer::createLocalDeploymentHandler, this));
    _cancelLocalDeploymentSubs = ggapi::Subscription::subscribeToTopic(
        keys.cancelLocalDeployment,
        ggapi::TopicCallback::of(&CliServer::cancelLocalDeploymentHandler, this));
    _getLocalDeploymentStatusSubs = ggapi::Subscription::subscribeToTopic(
        keys.getLocalDeploymentStatus,
        ggapi::TopicCallback::of(&CliServer::getLocalDeploymentStatusHandler, this));
    _listLocalDeploymentsSubs = ggapi::Subscription::subscribeToTopic(
        keys.listLocalDeployments,
        ggapi::TopicCallback::of(&CliServer::listLocalDeploymentsHandler, this));
    _listDeploymentsSubs = ggapi::Subscription::subscribeToTopic(
        keys.listDeployments, ggapi::TopicCallback::of(&CliServer::listDeploymentsHandler, this));
    // GG-Interop: Extract rootpath from system config
    std::filesystem::path rootPath = _system.getValue<std::string>({"rootpath"});
    // TODO: This should be async?
    generateCliIpcInfo(rootPath / CLI_IPC_INFO_PATH);
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

    interfaces::ipc_auth_info::IpcAuthInfoIn authIn;
    authIn.serviceName = SERVICE_NAME;
    auto request = ggapi::serialize(authIn);
    auto resultFuture =
        ggapi::Subscription::callTopicFirst(interfaces::ipc_auth_info::interfaceTopic, request);
    // TODO: handle case of resultFuture {} / no data returned
    auto result = ggapi::Struct(resultFuture.waitAndGetValue());
    interfaces::ipc_auth_info::IpcAuthInfoOut authOut;
    ggapi::deserialize(result, authOut);

    _clientIdToAuthToken.insert({clientId, authOut.authToken});

    ggapi::Struct ipcInfo = ggapi::Struct::create();
    ipcInfo.put(keys.cliAuthToken, authOut.authToken);
    ipcInfo.put(
        keys.socketPath,
        authOut.socketPath); // TODO: override socket path from recipe or nucleus config

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

void CliServer::onStop(ggapi::Struct data) {
    // TODO: call close on all LPCs
}

ggapi::ObjHandle CliServer::createLocalDeploymentHandler(
    ggapi::Symbol, const ggapi::Container &request) {
    auto deploymentDocument = ggapi::Struct{request};
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

    auto channel = ggapi::Channel::create();
    _subscriptions.emplace_back(deploymentId, channel, [](ggapi::Struct req) { return req; });
    channel.addCloseCallback([this, channel]() {
        std::unique_lock lock(_subscriptionMutex);
        auto it = std::find_if(_subscriptions.begin(), _subscriptions.end(), [channel](auto sub) {
            return std::get<1>(sub) == channel;
        });
        std::iter_swap(it, std::prev(_subscriptions.end()));
        _subscriptions.pop_back();
    });
    auto resultFuture =
        ggapi::Subscription::callTopicFirst(keys.createDeploymentTopicName, deployment);
    if(!resultFuture) {
        return {};
    }
    return resultFuture.andThen([deploymentId, channel](
                                    ggapi::Promise nextPromise, const ggapi::Future &prevFuture) {
        nextPromise.fulfill([&]() {
            ggapi::Struct result{prevFuture.getValue()};
            if(result.getValue<bool>({"status"})) {
                auto message = ggapi::Struct::create();
                message.put("deploymentId", deploymentId);
                return ggapi::Struct::create().put(keys.channel, channel).put(keys.shape, message);
            } else {
                // TODO Deprecate "status" / Correct error
                throw ggapi::ipc::ServiceError("Deployment failed");
            }
        });
    });
}

ggapi::ObjHandle CliServer::listDeploymentsHandler(ggapi::Symbol, const ggapi::Container &request) {
    auto deploymentDocument = ggapi::Struct{request};
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
    auto channel = ggapi::Channel::create();
    auto resultFuture =
        ggapi::Subscription::callTopicFirst(keys.createDeploymentTopicName, deployment);
    if(!resultFuture) {
        return {};
    }
    return resultFuture.andThen([requestId, channel](
                                    ggapi::Promise nextPromise, const ggapi::Future &prevFuture) {
        nextPromise.fulfill([&]() {
            ggapi::Struct result{prevFuture.getValue()};
            if(result.getValue<bool>({"status"})) {
                auto message = ggapi::Struct::create();
                message.put("deploymentId", requestId);
                return ggapi::Struct::create().put(keys.channel, channel).put(keys.shape, message);
            } else {
                // TODO Deprecate "status" / Correct error
                throw ggapi::ipc::ServiceError("Deployment failed");
            }
        });
    });
}

ggapi::ObjHandle CliServer::cancelLocalDeploymentHandler(ggapi::Symbol, const ggapi::Container &) {
    // TODO: Not implemented
    auto resultFuture = ggapi::Subscription::callTopicFirst(
        keys.cancelDeploymentTopicName, ggapi::Struct::create());
    return resultFuture.andThen([](ggapi::Promise nextPromise, const ggapi::Future &) {
        // ignores prev value
        nextPromise.fulfill(ggapi::Struct::create);
    });
}

ggapi::ObjHandle CliServer::getLocalDeploymentStatusHandler(
    ggapi::Symbol, const ggapi::Container &) {
    // TODO: Not implemented
    return ggapi::Struct::create();
}

ggapi::ObjHandle CliServer::listLocalDeploymentsHandler(ggapi::Symbol, const ggapi::Container &) {
    // TODO: Not implemented
    return ggapi::Struct::create();
}
