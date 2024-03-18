#include "deployment_manager.hpp"
#include "lifecycle/kernel.hpp"
#include "logging/log_queue.hpp"
#include "scope/context_full.hpp"
#include <filesystem>
#include <fstream>
#include <regex>
#include <system_error>
#include <temp_module.hpp>
#include <util.hpp>

const auto LOG = // NOLINT(cert-err58-cpp)
    logging::Logger::of("com.aws.greengrass.lifecycle.Deployment");

// TODO: Refactor into different scope
#if defined(__linux__)
static constexpr std::string_view PLATFORM_NAME = "linux";
#elif defined(_WIN32)
static constexpr std::string_view PLATFORM_NAME = "windows";
#elif defined(__APPLE__)
static constexpr std::string_view PLATFORM_NAME = "darwin";
#else
static constexpr std::string_view PLATFORM_NAME = "unknown";
#endif

namespace deployment {
    DeploymentManager::DeploymentManager(
        const scope::UsingContext &context, lifecycle::Kernel &kernel)
        : scope::UsesContext(context), _module(util::TempModule::create("DeploymentManager")),
          _kernel(kernel) {
        _deploymentQueue = std::make_shared<data::SharedQueue<std::string, Deployment>>(context);
        _componentStore = std::make_shared<data::SharedQueue<std::string, Recipe>>(context);
    }

    void DeploymentManager::start() {
        util::TempModule moduleScope(_module);
        std::unique_lock guard{_mutex};
        _createSubs = ggapi::Subscription::subscribeToTopic(
            ggapi::Symbol(CREATE_DEPLOYMENT_TOPIC_NAME.toSymbol().asInt()),
            ggapi::TopicCallback::of(&DeploymentManager::createDeploymentHandler, this));
        _cancelSubs = ggapi::Subscription::subscribeToTopic(
            ggapi::Symbol(CANCEL_DEPLOYMENT_TOPIC_NAME.toSymbol().asInt()),
            ggapi::TopicCallback::of(&DeploymentManager::cancelDeploymentHandler, this));

        _thread = std::thread(&DeploymentManager::listen, this, _module);
    }

    void DeploymentManager::stop() {
        util::TempModule moduleScope(_module);
        _terminate = true;
        _wake.notify_all();
        _thread.join();
    }

    void DeploymentManager::clearQueue() {
        util::TempModule moduleScope(_module);
        std::unique_lock guard{_mutex};
        _deploymentQueue->clear();
    }

    void DeploymentManager::listen(const ggapi::ModuleScope &module) {
        // TODO: Use component store
        scope::thread()->changeContext(context());
        std::ignore = module.setActive();
        std::unique_lock guard(_mutex);
        //_wake.wait(guard, [this]() { return !_deploymentQueue->empty() || _terminate; });
        while(!_terminate) {
            _wake.wait(guard, [this]() { return !_deploymentQueue->empty() || _terminate; });
            if(!_deploymentQueue->empty()) {
                const auto &nextDeployment = _deploymentQueue->next();
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
            //std::this_thread::sleep_for(POLLING_FREQUENCY);
        }
    }

    void DeploymentManager::createNewDeployment(const Deployment &deployment) {
        const auto &deploymentId = deployment.id;
        auto deploymentType = deployment.deploymentType;
        // TODO: Greengrass deployment id
        // TODO: persist and publish deployment status
        // TODO: Create deployment task?
        LOG.atInfo("deployment")
            .kv(DEPLOYMENT_ID_LOG_KEY, deploymentId)
            .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, deploymentId)
            .kv("DeploymentType", "LOCAL")
            .log("Received deployment in the queue");

        if(deploymentType == DeploymentType::LOCAL) {
            try {
                const auto &requiredCapabilities = deployment.deploymentDocumentObj.requiredCapabilities;
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

    void DeploymentManager::loadRecipesAndArtifacts(const Deployment &deployment) {
        auto &deploymentDocument = deployment.deploymentDocumentObj;
        if(!deploymentDocument.recipeDirectoryPath.empty()) {
            const auto &recipeDir = deploymentDocument.recipeDirectoryPath;
            copyAndLoadRecipes(recipeDir);
        }
        if(!deploymentDocument.artifactsDirectoryPath.empty()) {
            const auto &artifactsDir = deploymentDocument.artifactsDirectoryPath;
            copyArtifacts(artifactsDir);
        }
    }

    void DeploymentManager::copyAndLoadRecipes(const std::filesystem::path &recipeDir) {
        std::error_code ec{};
        auto iter = std::filesystem::directory_iterator(recipeDir, ec);
        if(ec != std::error_code{}) {
            LOG.atError()
                .event("recipe-load-failure")
                .kv("message", ec.message())
                .logAndThrow(std::filesystem::filesystem_error{ec.message(), recipeDir, ec});
        }

        for(const auto &entry : iter) {
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
        using Environment = std::unordered_map<std::string, std::optional<std::string>>;
        // TODO: More streamlined deployment task
        // TODO: Get non-target group to root packages group
        // TODO: Component manager - resolve version, prepare packages, ...
        const auto &currentDeployment = _deploymentQueue->next();
        const auto &currentRecipe = _componentStore->next();

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
        // TODO: This needs to be a generic map compare
        auto manifests = currentRecipe.getManifests();

        auto it = std::find_if(manifests.begin(), manifests.end(), [](const auto &manifest) {
            return manifest.platform.os.empty() || manifest.platform.os == PLATFORM_NAME
                   || manifest.platform.os == "*";
        });
        // TODO: and the nucleus type is lite?
        if(it == manifests.end()) {
            LOG.atError("deployment")
                .kv(DEPLOYMENT_ID_LOG_KEY, currentDeployment.id)
                .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, currentDeployment.id)
                .kv("DeploymentType", "LOCAL")
                .log("Platform not supported!");
            return;
        }

        LifecycleSection lifecycle;
        data::Archive::readFromStruct(it->lifecycle, lifecycle);

        // set global env
        Environment globalEnv;
        if(lifecycle.envMap.has_value()) {
            globalEnv.insert(lifecycle.envMap->begin(), lifecycle.envMap->end());
        }


        // TODO: Lifecycle management
        std::array<std::pair<std::optional<ScriptSection> *, std::string_view>, 4> sections = {
            std::pair(&lifecycle.install, "install"),
            std::pair(&lifecycle.run, "run"),
            std::pair(&lifecycle.startup, "startup"),
            std::pair(&lifecycle.shutdown, "shutdown") /*, "recover", "bootstrap" */};
        for(auto &section : sections) {
            if(section.first->has_value()) {
                using namespace std::chrono_literals;
                auto &step = section.first->value();
                std::string stepName{section.second};

                // execute each lifecycle phase
                auto deploymentRequest = ggapi::Struct::create();

                if(step.skipIf.has_value()) {
                    auto skipIf = util::splitWith(step.skipIf.value(), ' ');
                    if(!skipIf.empty()) {
                        std::string cmd = util::lower(skipIf[0]);
                        // skip the step if the executable exists on path
                        if(cmd == on_path_prefix) {
                            const auto &executable = skipIf[1];
                            // TODO: This seems so odd here? - code does nothing
                            auto envList = ggapi::List::create();
                            envList.put(0, executable);
                            auto request = ggapi::Struct::create();
                            request.put("GetEnv", envList);
                            // TODO: Skipif
                        }
                        // skip the step if the file exists
                        else if(cmd == exists_prefix) {
                            if(std::filesystem::exists(skipIf[1])) {
                                return;
                            }
                        }
                        // TODO: what if sub-command not recognized?
                    }
                }

                auto pid = std::invoke([&]() -> ipc::ProcessId {
                    // TODO: This needs a cleanup

                    auto getSetEnv =
                        [&]() -> std::unordered_map<std::string, std::optional<std::string>> {
                        Environment localEnv;
                        if(lifecycle.envMap.has_value()) {
                            localEnv.insert(lifecycle.envMap->begin(), lifecycle.envMap->end());
                        }
                        // Append global entries where local entries do not exist
                        localEnv.insert(globalEnv.begin(), globalEnv.end());
                        return localEnv;
                    };

                    // script
                    auto getScript = [&]() -> std::string {
                        auto script = std::regex_replace(
                            step.script,
                            std::regex(R"(\{artifacts:path\})"),
                            artifactPath.string());

                        if(defaultConfig && !defaultConfig->empty()) {
                            for(auto key : defaultConfig->getKeys()) {
                                auto value = defaultConfig->get(key);
                                if(value.isScalar()) {
                                    script = std::regex_replace(
                                        script,
                                        std::regex(R"(\{configuration:\/)" + key + R"(\})"),
                                        value.getString());
                                }
                            }
                        }

                        return script;
                    };

                    bool requirePrivilege = false;
                    std::chrono::seconds timeout{120};

                    // privilege
                    if(step.requiresPrivilege.has_value() && step.requiresPrivilege.value()) {
                        requirePrivilege = true;
                        deploymentRequest.put("RequiresPrivilege", requirePrivilege);
                    }

                    // timeout
                    if(step.timeout.has_value()) {
                        deploymentRequest.put("Timeout", step.timeout.value());
                        timeout = std::chrono::seconds{step.timeout.value()};
                    }

                    return _kernel.startProcess(
                        getScript(),
                        timeout,
                        requirePrivilege,
                        getSetEnv(),
                        currentRecipe.componentName);
                });

                if(pid) {
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

        // gets here only if all lifecycle steps are executed successfully
        LOG.atInfo("deployment")
            .kv(DEPLOYMENT_ID_LOG_KEY, currentDeployment.id)
            .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, currentDeployment.id)
            .kv("DeploymentType", "LOCAL")
            .log("Successfully deployed the component!");
    }

    ggapi::ObjHandle DeploymentManager::createDeploymentHandler(
        ggapi::Symbol, const ggapi::Container &deploymentContainer) {

        ggapi::Struct deploymentStruct{deploymentContainer};
        std::unique_lock guard{_mutex};
        Deployment deployment;
        try {
            // TODO: validate deployment
            // TODO: Document is intended to be a Struct (loaded document), not a file / string
            auto deploymentDocumentJson = deploymentStruct.get<std::string>("deploymentDocument");
            auto jsonToStruct = [](auto json) {
                auto container = ggapi::Buffer::create().insert(-1, util::Span{json}).fromJson();
                return ggapi::Struct{container};
            };
            auto deploymentDocumentStruct = jsonToStruct(deploymentDocumentJson);
            auto deploymentDocument = scope::context()->objFromInt<data::StructModelBase>(deploymentDocumentStruct.getHandleId());

            data::Archive::readFromStruct(deploymentDocument, deployment.deploymentDocumentObj);

            deployment.id = deploymentStruct.get<std::string>("id");
            deployment.isCancelled = deploymentStruct.get<bool>("isCancelled");
            deployment.deploymentStage =
                DeploymentStageMap.lookup(deploymentStruct.get<std::string>("deploymentStage"))
                    .value_or(DeploymentStage::DEFAULT);
            // TODO: This needs to be checked (optional access)
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
            _wake.notify_one();
        } else {
            const auto &deploymentPresent = _deploymentQueue->get(deployment.id);
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

    ggapi::ObjHandle DeploymentManager::cancelDeploymentHandler(
        ggapi::Symbol, const ggapi::Container &deploymentContainer) {

        ggapi::Struct deployment{deploymentContainer};
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
