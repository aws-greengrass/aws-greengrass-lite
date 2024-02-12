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
        try {
            return _recipeLoader.read(recipeFile);
        } catch(std::exception &e) {
            LOG.atWarn("deployment").kv("DeploymentType", "LOCAL").logAndThrow(e);
        }
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

        // component name is not recommended to start with "aws.greengrass"
        if(util::startsWith(currentRecipe.getComponentName(), "aws.greengrass")) {
            LOG.atWarn("deployment")
                .kv(DEPLOYMENT_ID_LOG_KEY, currentDeployment.id)
                .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, currentDeployment.id)
                .kv("DeploymentType", "LOCAL")
                .log("Given component name has conflict with plugin names");
        }

        auto artifactPath = _kernel.getPaths()->componentStorePath() / "artifacts"
                            / currentRecipe.componentName / currentRecipe.componentVersion;
        LOG.atInfo("deployment")
            .kv(DEPLOYMENT_ID_LOG_KEY, currentDeployment.id)
            .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, currentDeployment.id)
            .kv("DeploymentType", "LOCAL")
            .log("Starting deployment task");

        // get the default config
        auto defaultConfig = currentRecipe.getComponentConfiguration().defaultConfiguration;

        // TODO: Support other platforms
        auto manifests = currentRecipe.getManifests();
        auto it = std::find_if(manifests.begin(), manifests.end(), [](auto &manifest) {
            return manifest.platform.os == "linux";
        });
        // Only run if linux platform is supported
        // TODO: and the nucleus type is lite?
        if(it != manifests.end()) {
            auto getEnvironment = [](auto &environment) {
                if(environment->empty()) {
                    return ggapi::List::create();
                }
                auto envList = ggapi::List::create();
                int idx = 0;
                for(auto &name : environment->getKeys()) {
                    envList.put(
                        idx,
                        ggapi::Struct::create().put(
                            name.toString(), environment->get(name).getString()));
                    idx++;
                }
                return envList;
            };

            // execute each lifecycle phase
            auto deploymentRequest = ggapi::Struct::create();

            // set global env
            if(it->lifecycle.find("SetEnv") != it->lifecycle.end()) {
                auto envStruct = std::dynamic_pointer_cast<data::SharedStruct>(
                    it->lifecycle.at("SetEnv").getStruct());
                auto envList = getEnvironment(envStruct);
                ggapi::Struct request = ggapi::Struct::create();
                request.put("SetEnv", envList);
                std::ignore = ggapi::Task::sendToTopic(SET_ENVIRONMENT_TOPIC, request);
                return;
            }

            // TODO: Lifecycle management
            for(std::string stepName :
                {"install", "run", "startup", "shutdown", "recover", "bootstrap"}) {
                if(it->lifecycle.find(stepName) != it->lifecycle.end()) {
                    auto step = it->lifecycle.at(stepName);
                    if(step.isContainer()) {
                        std::shared_ptr<data::SharedStruct> command =
                            std::dynamic_pointer_cast<data::SharedStruct>(step.getStruct());

                        if(command->hasKey("SkipIf")
                           && !command->get("SkipIf").getString().empty()) {
                            auto skipIf = util::splitWith(command->get("skipif").getString(), ' ');
                            if(!skipIf.empty()) {
                                // skip the step if the executable exists on path
                                if(skipIf[0] == on_path_prefix) {
                                    const auto &executable = skipIf[1];
                                    auto envList = ggapi::List::create();
                                    envList.put(0, executable);
                                    auto request = ggapi::Struct::create();
                                    request.put("getenv", envList);
                                    auto response =
                                        ggapi::Task::sendToTopic(GET_ENVIRONMENT_TOPIC, request);
                                    if(response.get<bool>("status")) {
                                        return;
                                    }
                                }
                                // skip the step if the file exists
                                else if(skipIf[0] == exists_prefix) {
                                    if(std::filesystem::exists(skipIf[1])) {
                                        return;
                                    }
                                }
                            }
                        }

                        // env
                        if(command->hasKey("SetEnv")
                           && !command->get("SetEnv").getString().empty()) {
                            auto envStruct = std::dynamic_pointer_cast<data::SharedStruct>(
                                command->get("SetEnv").getStruct());
                            auto envList = getEnvironment(envStruct);
                            deploymentRequest.put("SetEnv", envList);
                        }

                        // script
                        if(command->hasKey("Script")
                           && !command->get("Script").getString().empty()) {
                            auto script = std::regex_replace(
                                command->get("script").getString(),
                                std::regex(R"(\{artifacts:path\})"),
                                artifactPath.string());
                            auto defaultConfig =
                                currentRecipe.getComponentConfiguration().defaultConfiguration;
                            for(auto key : defaultConfig->getKeys()) {
                                script = std::regex_replace(
                                    script,
                                    std::regex(R"(\{configuration:\/)" + key + R"(\})"),
                                    defaultConfig->get(key).getString());
                            }
                            deploymentRequest.put("Script", script);
                        }

                        // privilege
                        if(command->hasKey("RequiresPrivilege")) {
                            deploymentRequest.put(
                                "RequiresPrivilege", command->get("RequiresPrivilege").getBool());
                        }

                        // timeout
                        if(command->hasKey("Timeout")) {
                            deploymentRequest.put("Timeout", command->get("Timeout").getInt());
                        }
                    } else {
                        auto script = std::regex_replace(
                            step.getString(),
                            std::regex(R"(\{artifacts:path\})"),
                            artifactPath.string());
                        for(auto key : defaultConfig->getKeys()) {
                            script = std::regex_replace(
                                script,
                                std::regex(R"(\{configuration:\/)" + key + R"(\})"),
                                defaultConfig->get(key).getString());
                        }

                        // TODO: run doesn't have timeout
                        deploymentRequest.put("Script", script);
                        deploymentRequest.put("Timeout", 120); // defaults
                        deploymentRequest.put("RequiresPrivilege", false);
                        deploymentRequest.put("SetEnv", ggapi::List::create());
                    }

                    ggapi::Struct response =
                        ggapi::Task::sendToTopic(EXECUTE_PROCESS_TOPIC, deploymentRequest);
                    if(response.get<bool>("status")) {
                        LOG.atInfo("deployment")
                            .kv(DEPLOYMENT_ID_LOG_KEY, currentDeployment.id)
                            .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, currentDeployment.id)
                            .kv("DeploymentType", "LOCAL")
                            .log("Executed " + stepName + " step of the lifecycle");
                    } else {
                        LOG.atError("deployment")
                            .kv(DEPLOYMENT_ID_LOG_KEY, currentDeployment.id)
                            .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, currentDeployment.id)
                            .kv("DeploymentType", "LOCAL")
                            .log("Failed to execute " + stepName + " step of the lifecycle");
                        return; // if any of the lifecycle step fails, stop the deployment
                    }
                }
            }
        } else {
            LOG.atError("deployment")
                .kv(DEPLOYMENT_ID_LOG_KEY, currentDeployment.id)
                .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, currentDeployment.id)
                .kv("DeploymentType", "LOCAL")
                .log("Platform not supported!");
        }

        // gets here only if all lifecycle steps are executed successfully
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
                .log("Invalid deployment request. Please check you recipe.");
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
