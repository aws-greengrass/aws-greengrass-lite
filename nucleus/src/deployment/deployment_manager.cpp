#include "deployment_manager.hpp"
#include "logging/log_queue.hpp"
#include <filesystem>
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
        using namespace std::chrono_literals;
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
                            throw DeploymentException("Not implemented");
                        } else if(deploymentType == DeploymentType::IOT_JOBS) {
                            throw DeploymentException("Not implemented");
                        }
                    }
                }
                _deploymentQueue->pop();
            }
            std::this_thread::sleep_for(2s);
        }
    }

    void DeploymentManager::createNewDeployment(const Deployment &deployment) {
        const auto deploymentId = deployment.id;
        const auto deploymentType = deployment.deploymentType;
        // TODO: Greengrass deployment id
        LOG.atInfo("deploy")
            .kv(DEPLOYMENT_ID_LOG_KEY, deploymentId)
            .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, deploymentId)
            .kv("DeploymentType", "DEFAULT")
            .log("Received deployment in the queue");

        if(deploymentType == DeploymentType::LOCAL) {
            try {
                loadRecipesAndArtifacts(deployment);
                LOG.atInfo("deploy")
                    .kv(DEPLOYMENT_ID_LOG_KEY, deploymentId)
                    .log("Started deployment execution");
            } catch(std::runtime_error &e) {
                throw DeploymentException(
                    "Unable to load recipes and/or artifacts :" + std::string{e.what()});
            }
        }
    }

    void DeploymentManager::cancelDeployment(const std::string &deploymentId) {
        LOG.atInfo("deploy")
            .kv(DEPLOYMENT_ID_LOG_KEY, deploymentId)
            .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, deploymentId)
            .kv("DeploymentType", "DEFAULT")
            .log("Canceling given deployment");
    }

    void DeploymentManager::loadRecipesAndArtifacts(const Deployment &deployment) {
        auto &deploymentDocument = deployment.deploymentDocument;
        if(!deploymentDocument.recipeDirectoryPath.empty()) {
            auto recipeDir = deploymentDocument.recipeDirectoryPath;
            copyAndLoadRecipes(recipeDir);
        }
        if(!deploymentDocument.artifactsDirectoryPath.empty()) {
            auto artifactsDir = deploymentDocument.artifactsDirectoryPath;
            copyArtifactsAndRun(artifactsDir);
        }
    }

    void DeploymentManager::copyAndLoadRecipes(std::string_view recipeDir) {
        for(const auto &entry : std::filesystem::directory_iterator(recipeDir)) {
            if(!entry.is_directory()) {
                Recipe recipe = loadRecipeFile(entry);
                saveRecipeFile(recipe);
                auto semVer = recipe.componentName + "-v" + recipe.componentVersion;
                const std::hash<std::string> hasher;
                auto hashValue = hasher(semVer);
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
        // parse recipe
        std::string ext = util::lower(recipeFile.extension().generic_string());
        if(ext == ".yaml" || ext == ".yml") {
            deployment::YamlReader yamlReader;
            auto recipeStruct = yamlReader.read(recipeFile);
            Recipe recipe;
            recipe.componentVersion = recipeStruct.get<std::string>("ComponentVersion");
            recipe.formatVersion = recipeStruct.get<std::string>("RecipeFormatVersion");
            recipe.componentName = recipeStruct.get<std::string>("ComponentName");
            recipe.description = recipeStruct.get<std::string>("ComponentDescription");
            recipe.publisher = recipeStruct.get<std::string>("ComponentPublisher");
            if(recipeStruct.hasKey("ComponentConfiguration")) {
                recipe.configuration.message =
                    recipeStruct.getValue<ggapi::Struct>({"ComponentConfiguration"})
                        .getValue<ggapi::Struct>({"DefaultConfiguration"})
                        .get<std::string>("Message");
            }
            auto linuxPlatform = recipeStruct.getValue<ggapi::Struct>({"Manifests"})
                                     .getValue<ggapi::Struct>({"0"})
                                     .getValue<ggapi::Struct>({"Lifecycle"});
            if(linuxPlatform.hasKey("install")) {
                recipe.installCommand.requiresPrivilege =
                    linuxPlatform.getValue<bool>({"install", "RequiresPrivilege"});
                recipe.installCommand.script =
                    linuxPlatform.getValue<std::string>({"install", "Script"});
                if(linuxPlatform.hasKey("Run")) {
                    recipe.runCommand.requiresPrivilege =
                        linuxPlatform.getValue<bool>({"Run", "RequiresPrivilege"});
                    recipe.runCommand.script =
                        linuxPlatform.getValue<std::string>({"Run", "Script"});
                }
            } else if(linuxPlatform.hasKey("run")) {
                recipe.runCommand.script = linuxPlatform.getValue<std::string>({"run"});
            }
            return recipe;
        } else {
            throw DeploymentException("Unsupported recipe file type");
        }
    }

    void DeploymentManager::saveRecipeFile(const deployment::Recipe &recipe) {
        auto saveRecipePath = _kernel.getPaths()->componentStorePath() / "recipes"
                              / recipe.componentName / recipe.componentVersion;
        if(!std::filesystem::exists(saveRecipePath)) {
            std::filesystem::create_directories(saveRecipePath);
        }
    }

    void DeploymentManager::copyArtifactsAndRun(std::string_view artifactsDir) {
        Recipe recipe = _componentStore->next();
        auto saveArtifactsPath = _kernel.getPaths()->componentStorePath() / "artifacts"
                                 / recipe.componentName / recipe.componentVersion;
        if(!std::filesystem::exists(saveArtifactsPath)) {
            std::filesystem::create_directories(saveArtifactsPath);
        }
        auto artifactsPath =
            std::filesystem::path{artifactsDir} / recipe.componentName / recipe.componentVersion;
        std::filesystem::copy(
            artifactsPath,
            saveArtifactsPath,
            std::filesystem::copy_options::recursive
                | std::filesystem::copy_options::overwrite_existing);

        // TODO: Run process command
        if(!recipe.installCommand.script.empty()) {
            auto requiresPrivilege = recipe.installCommand.requiresPrivilege;
            auto installCommand = std::regex_replace(
                recipe.installCommand.script,
                std::regex("\\{artifacts:path\\}"),
                saveArtifactsPath.string());
            ggapi::Struct request;
            request.put("requiresPrivilege", requiresPrivilege);
            request.put("script", installCommand);
            request.put("workDir", saveArtifactsPath.string());
            ggapi::Struct response = ggapi::Task::sendToTopic(EXECUTE_PROCESS_TOPIC, request);
        }
        if(!recipe.runCommand.script.empty()) {
            auto requiresPrivilege = recipe.runCommand.requiresPrivilege;
            auto runCommand = std::regex_replace(
                recipe.runCommand.script,
                std::regex("\\{artifacts:path\\}"),
                saveArtifactsPath.string());
            ggapi::Struct request;
            request.put("requiresPrivilege", requiresPrivilege);
            request.put("script", runCommand);
            request.put("workDir", saveArtifactsPath.string());
            ggapi::Struct response = ggapi::Task::sendToTopic(EXECUTE_PROCESS_TOPIC, request);
        }
    }

    ggapi::Struct DeploymentManager::createDeploymentHandler(
        ggapi::Task, ggapi::Symbol, ggapi::Struct deploymentStruct) {
        std::unique_lock guard{_mutex};
        Deployment deployment;
        try {
            // TODO: Do rest
            auto deploymentDocumentJson = deploymentStruct.get<std::string>("deploymentDocument");
            auto jsonToStruct = [](auto json) {
                auto container = ggapi::Buffer::create().insert(-1, util::Span{json}).fromJson();
                return container.getHandleId() ? container.template unbox<ggapi::Struct>()
                                               : throw std::runtime_error("");
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
                DeploymentStageMap.at(deploymentStruct.get<std::string>("deploymentStage"));
            deployment.deploymentType =
                DeploymentTypeMap.at(deploymentStruct.get<std::string>("deploymentType"));
        } catch(std::exception &e) {
            throw DeploymentException("Invalid deployment request " + std::string{e.what()});
        }
        bool returnStatus = true;

        // TODO: Shadow deployments use a special queue id
        if(!_deploymentQueue->exists(deployment.id)) {
            _deploymentQueue->push({deployment.id, deployment});
        } else {
            auto deploymentPresent = _deploymentQueue->get(deployment.id);
            if(checkValidReplacement(deploymentPresent, deployment)) {
                LOG.atInfo("deploy")
                    .kv(DEPLOYMENT_ID_LOG_KEY, deployment.id)
                    .kv(DISCARDED_DEPLOYMENT_ID_LOG_KEY, deploymentPresent.id)
                    .log("Replacing existing deployment");
            } else {
                LOG.atInfo("deploy")
                    .kv(DEPLOYMENT_ID_LOG_KEY, deployment.id)
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
        const Deployment &presentDeployment, const Deployment &offerDeployment) {
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
