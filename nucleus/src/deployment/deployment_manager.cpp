#include "deployment_manager.hpp"
#include "logging/log_queue.hpp"
#include <filesystem>
#include <util.hpp>

const auto LOG = // NOLINT(cert-err58-cpp)
    logging::Logger::of("com.aws.greengrass.lifecycle.Deployment");

namespace deployment {
    DeploymentManager::DeploymentManager(const scope::UsingContext &context) {
        std::ignore = ggapiSubscribeToTopic(
            ggapiGetCurrentCallScope(),
            CREATE_DEPLOYMENT_TOPIC_NAME.toSymbol().asInt(),
            ggapi::TopicCallback::of(&DeploymentManager::createDeploymentHandler, this)
                .getHandleId());
        std::ignore = ggapiSubscribeToTopic(
            ggapiGetCurrentCallScope(),
            CANCEL_DEPLOYMENT_TOPIC_NAME.toSymbol().asInt(),
            ggapi::TopicCallback::of(&DeploymentManager::cancelDeploymentHandler, this)
                .getHandleId());
        _deploymentQueue =
            std::make_shared<data::SharedQueue<ggapi::Symbol, ggapi::Struct>>(context);
    }

    void DeploymentManager::start() {
        std::unique_lock guard{_mutex};
        _thread = std::thread(&DeploymentManager::listen, this);
    }

    void DeploymentManager::stop() {
        _terminate = true;
        _wake.notify_all();
        _thread.join();
    }

    void DeploymentManager::clearQueue() {
        std::unique_lock guard{_mutex};
        _deploymentQueue->clear();
    }

    void DeploymentManager::listen() {
        using namespace std::chrono_literals;
        scope::thread()->changeContext(context());
        while(!_terminate) {
            if(!_deploymentQueue->empty()) {
                auto nextDeployment = _deploymentQueue->next();
                if(nextDeployment.get<bool>("isCancelled")) {
                    cancelDeployment(nextDeployment.get<std::string>("id"));
                } else {
                    auto deploymentType = nextDeployment.get<std::string>("deploymentType");
                    auto deploymentStage = nextDeployment.get<std::string>("deploymentStage");
                    if(deploymentStage == "DEFAULT") {
                        createNewDeployment(nextDeployment);
                    } else {
                        // TODO: Perform kernel update
                        if(deploymentType == "SHADOW") {
                            // TODO: Not implemented
                            return;
                        } else if(deploymentType == "IOT_JOBS") {
                            // TODO: Not implemented
                            return;
                        }
                    }
                }
                _deploymentQueue->pop();
            }
            std::this_thread::sleep_for(2s);
        }
    }

    void DeploymentManager::createNewDeployment(const ggapi::Struct &deployment) {
        const auto deploymentId = deployment.get<std::string>("id");
        const auto deploymentType = deployment.get<std::string>("deploymentType");
        // TODO: Greengrass deployment id
        LOG.atInfo("deploy")
            .kv(DEPLOYMENT_ID_LOG_KEY, deploymentId)
            .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, deploymentId)
            .kv("DeploymentType", deploymentType)
            .log("Received deployment in the queue");

        if(deploymentType == "LOCAL") {
            try {
                loadRecipesAndArtifacts(deployment);
            } catch(std::runtime_error &e) {
                throw DeploymentException("Unable to load recipes and/or artifacts");
            }
        }
    }

    void DeploymentManager::cancelDeployment(const std::string &deploymentId) {
    }

    void DeploymentManager::loadRecipesAndArtifacts(const ggapi::Struct &deployment) {
        // TODO: copy recipes and artifacts / create symlinks?
        auto deploymentDocumentJson = deployment.get<std::string>("deploymentDocument");
        auto deploymentDocument = [&deploymentDocumentJson] {
            auto container =
                ggapi::Buffer::create().insert(-1, util::Span{deploymentDocumentJson}).fromJson();
            return container.getHandleId() ? container.unbox<ggapi::Struct>()
                                           : throw std::runtime_error("");
        }();
        if(deploymentDocument.hasKey("recipeDirectoryPath")) {
            auto recipeDir = deploymentDocument.get<std::string>("recipeDirectoryPath");
            for(const auto &entry : std::filesystem::directory_iterator(recipeDir)) {
                if(!entry.is_directory()) {
                    getConfig().read(entry);
                }
            }
        }
        if(deploymentDocument.hasKey("artifactsDirectoryPath")) {
            auto artifactsDir = deploymentDocument.get<std::string>("artifactsDirectoryPath");
        }
    }

    ggapi::Struct DeploymentManager::createDeploymentHandler(
        ggapi::Task, ggapi::Symbol, ggapi::Struct deployment) {
        // TODO: ggapi::Struct to struct?
        std::unique_lock guard{_mutex};
        bool returnStatus = true;
        if(deployment.empty()) {
            throw DeploymentException("Invalid deployment request");
        }
        // TODO: Shadow deployments use a special queue id
        auto deploymentId = deployment.get<std::string>("id");
        if(!_deploymentQueue->exists(deploymentId)) {
            _deploymentQueue->push({deploymentId, deployment});
        } else {
            auto deploymentPresent = _deploymentQueue->get(deploymentId);
            if(checkValidReplacement(deploymentPresent, deployment)) {
                LOG.atInfo("deploy")
                    .kv(DEPLOYMENT_ID_LOG_KEY, deploymentId)
                    .kv(DISCARDED_DEPLOYMENT_ID_LOG_KEY, deploymentPresent.get<std::string>("id"))
                    .log("Replacing existing deployment");
            } else {
                LOG.atInfo("deploy")
                    .kv(DEPLOYMENT_ID_LOG_KEY, deploymentId)
                    .log("Deployment ignored because of duplicate");
                returnStatus = false;
            }
        }
        return ggapi::Struct::create().put("status", returnStatus);
    }

    ggapi::Struct DeploymentManager::cancelDeploymentHandler(
        ggapi::Task, ggapi::Symbol, ggapi::Struct deployment) {
        std::unique_lock guard{_mutex};
        if(deployment.empty()) {
            throw DeploymentException("Invalid deployment request");
        }
        auto deploymentId = deployment.get<std::string>("id");
        if(_deploymentQueue->exists(deploymentId)) {
            _deploymentQueue->remove(deploymentId);
        } else {
            throw DeploymentException("Deployment do not exist");
        }
        return ggapi::Struct::create().put("status", true);
    }

    bool DeploymentManager::checkValidReplacement(
        const ggapi::Struct &presentDeployment, const ggapi::Struct &offerDeployment) {
        if(presentDeployment.get<std::string>("deploymentType") == "DEFAULT") {
            return false;
        }
        if(offerDeployment.get<std::string>("deploymentType") == "SHADOW"
           || offerDeployment.get<bool>("isCancelled")) {
            return true;
        }
        return offerDeployment.get<std::string>("deploymentStage") == "DEFAULT";
    }
} // namespace deployment
