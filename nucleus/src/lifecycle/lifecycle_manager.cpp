#include "lifecycle_manager.hpp"
#include "lifecycle/kernel.hpp"
#include "package_manager/recipe_loader.hpp"
#include "package_manager/recipe_model.hpp"
#include <chrono>
#include <config/config_nodes.hpp>
#include <deployment/model/dependency_order.hpp>
#include <logging/log_manager.hpp>
#include <memory>
#include <mutex>
#include <pubsub/local_topics.hpp>
#include <pubsub/promise.hpp>
#include <scope/context_impl.hpp>
#include <shared_mutex>

[[maybe_unused]] static const auto &getLog() noexcept {
    static const auto log = logging::Logger::of("aws.greengrass.lifecycle");
    return log;
}

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
    [[maybe_unused]] static std::string qualifiedName(const Recipe &r) {
        return r.getComponentName() + "@" + r.getComponentVersion();
    }

    static std::vector<std::string> getDependencies(const Recipe &recipe) {
        std::vector<std::string> dependencies;
        dependencies.reserve(recipe.componentDependencies.size());
        std::transform(
            recipe.componentDependencies.begin(),
            recipe.componentDependencies.end(),
            std::back_inserter(dependencies),
            [](const auto &kv) -> std::string { return kv.first; });
        return dependencies;
    }
} // namespace deployment

namespace lifecycle {

    std::shared_ptr<plugins::AbstractPlugin> LifecycleManager::loadComponent(
        const deployment::Recipe &recipe) {
        auto ctx = context();
        if(recipe.componentType == "aws.greengrass.plugin") {
            return ctx->pluginLoader().loadNativePlugin(recipe);
        } else {

            // use LPC to load plugin
            auto componentTopic =
                ctx->configManager().lookupTopics({"services", recipe.componentName});
            auto recipePath = componentTopic->lookup({"recipePath"});
            auto recipeStruct =
                package_manager::RecipeLoader{}.readAsStruct(recipePath.getString());

            // get the default config
            auto defaultConfig = recipe.getComponentConfiguration().defaultConfiguration;

            // TODO: Support other platforms
            // TODO: This needs to be a generic map compare
            auto manifests = recipe.getManifests();

            /*
            Manifests:
            - Platform:
                nucleus: java
                Artifacts:
                - URI: s3://mock-bucket/java/java-stuff.zip
                - URI: s3://mock-bucket/shared/shared.zip
                Selections:
                - java
            */
            auto iterator =
                std::find_if(manifests.begin(), manifests.end(), [](const auto &manifest) {
                    return manifest.platform.os.empty() || manifest.platform.os == PLATFORM_NAME
                           || manifest.platform.os == "*";
                });

            // TODO: and the nucleus type is lite?
            if(iterator == manifests.end()) {
                getLog()
                    .atError("lifecycle")
                    .kv("componentName", recipe.componentName)
                    .log("Platform not supported!");
                return {};
            }

            int index = static_cast<int>(std::distance(manifests.begin(), iterator));
            auto selectedManifest = recipeStruct->get(recipeStruct->foldKey("Manifests", true))
                                        .castObject<data::ListModelBase>()
                                        ->get(index)
                                        .castObject<data::StructModelBase>();

            auto artifactPath = _kernel.getPaths()->componentStorePath() / "artifacts"
                                / recipe.componentName / recipe.componentVersion;

            std::cout << selectedManifest->toJson().get() << std::endl;

            auto data_pack = std::make_shared<data::SharedStruct>(ctx);
            data_pack->put("recipe", recipeStruct);
            data_pack->put("componentName", recipe.componentName);
            // TODO: get current deployment from deployment manager
            data_pack->put("deploymentId", "00000000-0000-0000-0000-000000000000");
            data_pack->put("manifest", selectedManifest);
            data_pack->put("artifactPath", artifactPath.generic_string());
            data_pack->put("defaultConfig", defaultConfig);

            using namespace std::string_literals;
            data::Symbol topic{ctx->intern("componentType::"s + recipe.componentType)};
            auto response = ctx->lpcTopics().callFirst(topic, data_pack);
            if(!response) {
                return {};
            }
            auto responseStruct = response->getValue()->ref<data::StructModelBase>();
            return responseStruct->get("moduleHandle").castObject<plugins::AbstractPlugin>();
        }
    }

    auto LifecycleManager::runLifecycleStep(const std::string &name, const data::Symbol &phase)
        -> LifecycleManager::LifecycleResult {
        std::unique_lock guard{_serviceMutex};
        auto iter = _services.active.find(name);
        if(iter == _services.active.end()) {
            return LifecycleResult::inactive;
        }
        auto component = iter->second;
        // Check that all dependencies are active
        if(const auto &dependencies = component->getDependencies(); std::any_of(
               dependencies.begin(),
               dependencies.end(),
               [this](std::string const &dependency) -> bool {
                   return _services.active.find(dependency) == _services.active.end();
               })) {
            _services.inactive.emplace(*iter);
            _services.active.erase(iter);
            return LifecycleResult::missingDependency;
        }
        guard.unlock();

        if(!component->lifecycle(phase, component->loader().buildParams(*component))) {
            std::unique_lock guard{_serviceMutex};
            _services.broken.emplace(*iter);
            _services.active.erase(iter);
            return LifecycleResult::failed;
        }
        return LifecycleResult::success;
    };

    size_t LifecycleManager::runLifecyclesToCompletion(std::vector<std::string> components) {
        auto ctx = context();
        auto &loader = ctx->pluginLoader();

        components.erase(
            std::remove_if(
                components.begin(),
                components.end(),
                [&loader, this](std::string const &name) {
                    return runLifecycleStep(name, loader.INITIALIZE) != LifecycleResult::success;
                }),
            components.end());

        components.erase(
            std::remove_if(
                components.begin(),
                components.end(),
                [&loader, this](std::string const &name) {
                    return runLifecycleStep(name, loader.START) != LifecycleResult::success;
                }),
            components.end());

        return components.size();
    }

    void LifecycleManager::loadComponents(
        std::launch type, const std::vector<deployment::Recipe> &recipes) {
        std::vector<std::pair<std::string, std::future<std::shared_ptr<plugins::AbstractPlugin>>>>
            futures;
        futures.reserve(recipes.size());
        std::transform(
            recipes.begin(),
            recipes.end(),
            std::back_inserter(futures),
            [this](const deployment::Recipe &recipe) {
                return std::make_pair(
                    recipe.componentName,
                    std::async(
                        std::launch::deferred, &LifecycleManager::loadComponent, this, recipe));
            });

        for(auto &&[name, future] : std::move(futures)) {
            auto plugin = [](const auto &name,
                             auto &&future) -> std::shared_ptr<plugins::AbstractPlugin> {
                try {
                    return future.get();
                } catch(...) {
                    getLog()
                        .atError("plugin-load-fail")
                        .kv("componentName", name)
                        .cause(std::current_exception())
                        .log();
                    return {};
                }
            }(name, std::move(future));
            std::unique_lock guard{_serviceMutex};
            if(plugin) {
                _services.active.emplace(std::move(name), std::move(plugin));
            } else {
                _services.broken.emplace(std::move(name), std::move(plugin));
            }
        }
    }

    std::future<bool> LifecycleManager::addTask(
        Request request, std::vector<std::string> components) {
        std::promise<bool> promise;
        auto future = promise.get_future();
        if(components.empty()) {
            promise.set_value(true);
        } else {
            std::unique_lock guard{_queueMutex};
            _workQueue.emplace(WorkItem{request, std::move(promise), std::move(components)});
            _cv.notify_one();
        }
        return future;
    }

    std::future<bool> LifecycleManager::runComponents(std::vector<std::string> recipes) {
        return addTask(Request::start, std::move(recipes));
    }

    std::future<bool> LifecycleManager::stopComponents(std::vector<std::string> recipes) {
        return addTask(Request::stop, std::move(recipes));
    }

    LifecycleManager::LifecycleManager(const scope::UsingContext &context, Kernel &kernel)
        : scope::UsesContext(context), _kernel{kernel} {
    }

    LifecycleManager::~LifecycleManager() noexcept {
        _terminate.store(true);
        _cv.notify_one();
        if(_worker.joinable()) {
            _worker.join();
        }
    }

    bool LifecycleManager::startComponentTask(std::vector<std::string> components) {
        using clock = std::chrono::high_resolution_clock;
        auto t1 = clock::now();
        std::vector<deployment::Recipe> recipes;
        recipes.reserve(components.size());
        auto serviceTopic = context()->configManager().lookupTopics({"services"});
        for(const auto &name : components) {
            {
                std::shared_lock guard{_serviceMutex};
                if(_services.active.count(name) > 0) {
                    continue;
                }
            }

            auto componentTopic = serviceTopic->lookupTopics({name});
            auto recipePath = componentTopic->lookup({"recipePath"}).getString();
            recipes.emplace_back(package_manager::RecipeLoader{}.read(recipePath));
        }

        // process plugins first
        auto middle =
            std::partition(recipes.begin(), recipes.end(), [](const deployment::Recipe &lhs) {
                using namespace std::string_view_literals;
                return lhs.componentType == "aws.greengrass.plugin"sv;
            });
        // group remaining components by component type
        std::sort(
            middle,
            recipes.end(),
            [](const deployment::Recipe &lhs, const deployment::Recipe &rhs) {
                using namespace std::string_view_literals;
                return lhs.componentType < rhs.componentType;
            });
        // build initial graph with just native plugins
        std::unordered_map<std::string, deployment::Recipe> unresolved;
        // add each plugin to the unresolved map
        std::for_each(recipes.begin(), middle, [&unresolved](const deployment::Recipe &recipe) {
            unresolved.emplace(recipe.componentName, recipe);
        });
        auto runOrder = util::DependencyOrder{}.computeOrderedDependencies(
            unresolved, deployment::getDependencies);

        // Mark each still-unresolved plugin as inactive, remove from map
        std::invoke([&, this] {
            if(!unresolved.empty()) {
                std::unique_lock guard{_serviceMutex};
                for(const auto &[name, _] : unresolved) {
                    _services.inactive.emplace(name, nullptr);
                }
                unresolved.clear();
            }
        });

        // load and start all plugins
        std::invoke(
            [this](const std::vector<deployment::Recipe> &recipes) {
                loadComponents(std::launch::deferred, recipes);
                std::vector<std::string> names;
                names.reserve(recipes.size());
                for(const auto &recipe : recipes) {
                    names.emplace_back(recipe.componentName);
                }
                runLifecyclesToCompletion(std::move(names));
            },
            runOrder->values());

        // Add each successive component type group into the graph
        while(middle != recipes.end()) {
            auto last = std::partition_point(middle, recipes.end(), [&middle](const auto &recipe) {
                return middle->componentType == recipe.componentType;
            });

            std::for_each(middle, last, [&](const deployment::Recipe &recipe) {
                unresolved.emplace(recipe.componentName, recipe);
            });
            util::DependencyOrder{}.computeOrderedDependencies(
                *runOrder, unresolved, deployment::getDependencies);

            middle = last;
        }

        // Mark each still-unresolved components as inactive, remove from map
        std::invoke([&, this] {
            if(!unresolved.empty()) {
                std::unique_lock guard{_serviceMutex};
                for(const auto &[name, _] : unresolved) {
                    _services.inactive.emplace(name, nullptr);
                }
                unresolved.clear();
            }
        });

        // load and start all other components
        std::invoke(
            [this](std::vector<deployment::Recipe> recipes) {
                recipes.erase(
                    std::remove_if(
                        recipes.begin(),
                        recipes.end(),
                        [](const deployment::Recipe &recipe) {
                            return recipe.componentType == "aws.greengrass.plugin";
                        }),
                    recipes.end());

                if(recipes.empty()) {
                    return;
                }

                loadComponents(std::launch::async, recipes);
                std::vector<std::string> names;
                names.reserve(recipes.size());
                for(const auto &recipe : recipes) {
                    names.emplace_back(recipe.componentName);
                }
                runLifecyclesToCompletion(std::move(names));
            },
            runOrder->values());

        // verify all requested components are running
        std::shared_lock guard{_serviceMutex};
        auto result =
            std::all_of(components.begin(), components.end(), [this](const std::string &name) {
                return _services.active.count(name) == 1;
            });
        guard.unlock();
        auto msTaken =
            std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t1).count();
        getLog().atInfo().kv("milliseconds", msTaken).log();
        return result;
    }

    void LifecycleManager::lifecycleQueueThread() noexcept {
        while(!_terminate.load()) {
            std::unique_lock guard{_queueMutex};
            _cv.wait(guard, [this]() { return !_workQueue.empty() || _terminate.load(); });
            if(_terminate.load()) {
                break;
            }

            auto [request, result, names] = std::move(_workQueue.front());
            _workQueue.pop();
            guard.unlock();

            try {
                result.set_value(startComponentTask(std::move(names)));
            } catch(...) {
                result.set_value(false);
                getLog()
                    .atError("lifecycle-failed")
                    .cause(std::current_exception())
                    .log("Failed to start components");
                // result.set_exception(std::current_exception);
            }
        }
    }

} // namespace lifecycle
