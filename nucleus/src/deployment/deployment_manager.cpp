#include "deployment_manager.hpp"
#include "logging/log_queue.hpp"
#include <filesystem>
#include <fstream>
#include <regex>
#include <util.hpp>

const auto LOG = // NOLINT(cert-err58-cpp)
    logging::Logger::of("com.aws.greengrass.lifecycle.Deployment");

namespace deployment {
    DeploymentManager::DeploymentManager(
        const scope::UsingContext &context, lifecycle::Kernel &kernel)
        : scope::UsesContext(context), _kernel(kernel) {
        _deploymentQueue = std::make_shared<data::SharedQueue<std::string, Deployment>>(context);
        _componentStore = std::make_shared<data::SharedQueue<std::string, Recipe>>(context);
    }

    void DeploymentManager::start() {
        std::unique_lock guard{_mutex};
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
        scope::thread()->changeContext(context());
        while(!_terminate) {
            if(!_deploymentQueue->empty()) {
                auto nextDeployment = _deploymentQueue->next();
                if(nextDeployment.isCancelled) {
                    cancelDeployment(nextDeployment.id);
                } else {
                    auto deploymentType = nextDeployment.deploymentType;
                    auto deploymentStage = nextDeployment.deploymentStage;

                    if(deploymentStage == DeploymentStage::DEFAULT) {
                        createNewDeployment(nextDeployment);
                    } else {
                        // TODO: Perform kernel update
                        if(deploymentType == DeploymentType::SHADOW) {
                            // TODO: Not implemented
                            LOG.atInfo("deployment")
                                .kv(DEPLOYMENT_ID_LOG_KEY, nextDeployment.id)
                                .log("Unsupported deployment type");
                        } else if(deploymentType == DeploymentType::IOT_JOBS) {
                            // TODO: Not implemented
                            LOG.atInfo("deployment")
                                .kv(DEPLOYMENT_ID_LOG_KEY, nextDeployment.id)
                                .log("Unsupported deployment type");
                        }
                    }
                }
                runDeploymentTask();
                _deploymentQueue->pop();
            }
            std::this_thread::sleep_for(POLLING_FREQUENCY);
        }
    }

    void DeploymentManager::createNewDeployment(const Deployment &deployment) {
        const auto deploymentId = deployment.id;
        auto deploymentType = deployment.deploymentType;
        // TODO: Greengrass deployment id
        // TODO: persist and publish deployment status
        // TODO: Get non-target group to root packages group
        // TODO: Component manager - resolve version, prepare packages, ...
        LOG.atInfo("deployment")
            .kv(DEPLOYMENT_ID_LOG_KEY, deploymentId)
            .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, deploymentId)
            .kv("DeploymentType", "LOCAL")
            .log("Received deployment in the queue");

        if(deploymentType == DeploymentType::LOCAL) {
            try {
                auto requiredCapabilities = deployment.deploymentDocument.requiredCapabilities;
                if(!requiredCapabilities.empty()) {
                    // TODO: check if required capabilities are supported
                }
                loadRecipesAndArtifacts(deployment);
            } catch(std::runtime_error &e) {
                LOG.atError("deployment")
                    .kv(DEPLOYMENT_ID_LOG_KEY, deploymentId)
                    .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, deploymentId)
                    .kv("DeploymentType", "LOCAL")
                    .log(e.what());
            }
        }
    }

    void DeploymentManager::cancelDeployment(const std::string &deploymentId) {
        LOG.atInfo("deployment")
            .kv(DEPLOYMENT_ID_LOG_KEY, deploymentId)
            .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, deploymentId)
            .log("Canceling given deployment");
        // TODO: Kill the process doing the deployment
    }

    void DeploymentManager::resolveDependencies(deployment::DeploymentDocument) {
    }

    void DeploymentManager::loadRecipesAndArtifacts(const Deployment &deployment) {
        auto &deploymentDocument = deployment.deploymentDocument;
        if(!deploymentDocument.recipeDirectoryPath.empty()) {
            auto recipeDir = deploymentDocument.recipeDirectoryPath;
            copyAndLoadRecipes(recipeDir);
        }
        if(!deploymentDocument.artifactsDirectoryPath.empty()) {
            auto artifactsDir = deploymentDocument.artifactsDirectoryPath;
            copyArtifacts(artifactsDir);
        }
    }

    void DeploymentManager::copyAndLoadRecipes(std::string_view recipeDir) {
        for(const auto &entry : std::filesystem::directory_iterator(recipeDir)) {
            if(!entry.is_directory()) {
                Recipe recipe = loadRecipeFile(entry);
                saveRecipeFile(recipe);
                auto semVer = recipe.componentName + "-v" + recipe.componentVersion;
                const std::hash<std::string> hasher;
                auto hashValue = hasher(semVer); // TODO: Digest hashing algorithm
                _componentStore->push({semVer, recipe});
                auto saveRecipeName =
                    std::to_string(hashValue) + "@" + recipe.componentVersion + ".recipe.yml";
                auto saveRecipeDst = _kernel.getPaths()->componentStorePath() / "recipes"
                                     / recipe.componentName / recipe.componentVersion
                                     / saveRecipeName;
                std::filesystem::copy_file(
                    entry, saveRecipeDst, std::filesystem::copy_options::overwrite_existing);
            }
        }
    }

    Recipe DeploymentManager::loadRecipeFile(const std::filesystem::path &recipeFile) {
        return _recipeLoader.read(recipeFile);
    }

    void DeploymentManager::saveRecipeFile(const deployment::Recipe &recipe) {
        auto saveRecipePath = _kernel.getPaths()->componentStorePath() / "recipes"
                              / recipe.componentName / recipe.componentVersion;
        if(!std::filesystem::exists(saveRecipePath)) {
            std::filesystem::create_directories(saveRecipePath);
        }
    }

    void DeploymentManager::copyArtifacts(std::string_view artifactsDir) {
        Recipe recipe = _componentStore->next();
        auto saveArtifactPath = _kernel.getPaths()->componentStorePath() / "artifacts"
                                / recipe.componentName / recipe.componentVersion;
        if(!std::filesystem::exists(saveArtifactPath)) {
            std::filesystem::create_directories(saveArtifactPath);
        }
        auto artifactPath =
            std::filesystem::path{artifactsDir} / recipe.componentName / recipe.componentVersion;
        std::filesystem::copy(
            artifactPath,
            saveArtifactPath,
            std::filesystem::copy_options::recursive
                | std::filesystem::copy_options::overwrite_existing);
    }

    void DeploymentManager::runDeploymentTask() {
        // TODO: More streamlined deployment task
        auto currentDeployment = _deploymentQueue->next();
        auto currentRecipe = _componentStore->next();
        auto artifactPath = _kernel.getPaths()->componentStorePath() / "artifacts"
                            / currentRecipe.componentName / currentRecipe.componentVersion;
        LOG.atInfo("deployment")
            .kv(DEPLOYMENT_ID_LOG_KEY, currentDeployment.id)
            .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, currentDeployment.id)
            .kv("DeploymentType", "LOCAL")
            .log("Starting deployment task");

        // TODO: Support other platforms
        auto it = std::find_if(
            currentRecipe.manifests.begin(), currentRecipe.manifests.end(), [](auto &manifest) {
                return manifest.platform.os == "linux";
            });
        if(it != currentRecipe.manifests.end()) {
            for(const auto &[stage, command] : it->lifecycle) {
                auto script = std::regex_replace(
                    command.script, std::regex(R"(\{artifacts:path\})"), artifactPath.string());
                if(currentRecipe.configuration.defaultConfiguration.hasKey("message")) {
                    script = std::regex_replace(
                        script,
                        std::regex(R"(\{configuration:\/Message\})"),
                        currentRecipe.configuration.defaultConfiguration.get<std::string>(
                            "message"));
                }

                auto request = ggapi::Struct::create();
                request.put("requiresPrivilege", command.requiresPrivilege);
                request.put("script", script);
                request.put("workDir", artifactPath.string());
                ggapi::Struct response = ggapi::Task::sendToTopic(EXECUTE_PROCESS_TOPIC, request);
                if(response.get<bool>("status")) {
                    LOG.atInfo("deployment")
                        .kv(DEPLOYMENT_ID_LOG_KEY, currentDeployment.id)
                        .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, currentDeployment.id)
                        .kv("DeploymentType", "LOCAL")
                        .log("Executed " + stage + " of the lifecycle");
                }
            }
        } else {
            LOG.atInfo("deployment")
                .kv(DEPLOYMENT_ID_LOG_KEY, currentDeployment.id)
                .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, currentDeployment.id)
                .kv("DeploymentType", "LOCAL")
                .log("Platform not supported!");
        }

        LOG.atInfo("deployment")
            .kv(DEPLOYMENT_ID_LOG_KEY, currentDeployment.id)
            .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, currentDeployment.id)
            .kv("DeploymentType", "LOCAL")
            .log("Successfully deployed the component!");
    }

    ggapi::Struct DeploymentManager::createDeploymentHandler(
        ggapi::Task, ggapi::Symbol, ggapi::Struct deploymentStruct) {
        std::unique_lock guard{_mutex};
        Deployment deployment;
        try {
            // TODO: Do rest of the deployment
            // TODO: validate deployment
            auto deploymentDocumentJson = deploymentStruct.get<std::string>("deploymentDocument");
            auto jsonToStruct = [](auto json) {
                auto container = ggapi::Buffer::create().insert(-1, util::Span{json}).fromJson();
                return ggapi::Struct{container};
            };
            auto deploymentDocument = jsonToStruct(deploymentDocumentJson);

            deployment.id = deploymentStruct.get<std::string>("id");
            deployment.deploymentDocument.requestId = deployment.id;
            deployment.deploymentDocument.artifactsDirectoryPath =
                deploymentDocument.get<std::string>("artifactsDirectoryPath");
            deployment.deploymentDocument.recipeDirectoryPath =
                deploymentDocument.get<std::string>("recipeDirectoryPath");
            deployment.isCancelled = deploymentStruct.get<bool>("isCancelled");
            deployment.deploymentStage =
                DeploymentStageMap.lookup(deploymentStruct.get<std::string>("deploymentStage"))
                    .value_or(DeploymentStage::DEFAULT);
            deployment.deploymentType =
                DeploymentTypeMap.lookup(deploymentStruct.get<std::string>("deploymentType"))
                    .value();
        } catch(std::exception &e) {
            LOG.atError("deployment")
                .kv(DEPLOYMENT_ID_LOG_KEY, deployment.id)
                .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, deployment.id)
                .kv("DeploymentType", "LOCAL")
                .log("Invalid deployment request");
            return ggapi::Struct::create().put("status", false);
        }
        bool returnStatus = true;

        // TODO: Shadow deployments use a special queue id
        if(!_deploymentQueue->exists(deployment.id)) {
            _deploymentQueue->push({deployment.id, deployment});
        } else {
            auto deploymentPresent = _deploymentQueue->get(deployment.id);
            if(checkValidReplacement(deploymentPresent, deployment)) {
                LOG.atInfo("deployment")
                    .kv(DEPLOYMENT_ID_LOG_KEY, deployment.id)
                    .kv(DISCARDED_DEPLOYMENT_ID_LOG_KEY, deploymentPresent.id)
                    .log("Replacing existing deployment");
            } else {
                LOG.atInfo("deployment")
                    .kv(DEPLOYMENT_ID_LOG_KEY, deployment.id)
                    .log("Deployment ignored because of duplicate");
                returnStatus = false;
            }
        }

        // save deployment metadata to file
        auto deploymentPath = _kernel.getPaths()->deploymentPath();
        std::filesystem::create_directory(deploymentPath / deployment.id);
        std::ofstream ofstream(
            deploymentPath / deployment.id / "deployment_metadata.json", std::ios::trunc);
        ggapi::Buffer buffer = deploymentStruct.toJson();
        buffer.write(ofstream); // TODO: Use util::commitable
        ofstream.flush();
        ofstream.close();

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
        const Deployment &presentDeployment, const Deployment &offerDeployment) noexcept {
        if(presentDeployment.deploymentStage == DeploymentStage::DEFAULT) {
            return false;
        }
        if(offerDeployment.deploymentType == DeploymentType::SHADOW
           || offerDeployment.isCancelled) {
            return true;
        }
        return offerDeployment.deploymentStage != DeploymentStage::DEFAULT;
    }
} // namespace deployment
