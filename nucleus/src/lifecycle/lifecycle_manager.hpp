#pragma once

#include "deployment/recipe_model.hpp"
#include "plugins/plugin_loader.hpp"
#include <future>
#include <queue>
#include <scope/context.hpp>

namespace lifecycle {
    class Kernel;
    class LifecycleManager final : scope::UsesContext {
    private:
        Kernel &_kernel;

        enum class LifecycleResult { success, failed, inactive, missingDependency };
        enum class Request { start, stop };
        struct WorkItem {
            Request request;
            std::promise<bool> result;
            std::vector<std::string> components;
        };

    private:
        mutable std::mutex _queueMutex;
        mutable std::shared_mutex _serviceMutex;
        std::condition_variable _cv;
        std::queue<WorkItem> _workQueue;
        std::atomic_bool _terminate{false};
        std::thread _worker{&LifecycleManager::lifecycleQueueThread, this};

        void lifecycleQueueThread() noexcept;

        std::future<bool> addTask(Request request, std::vector<std::string> components);

        bool startComponentTask(std::vector<std::string> components);

        void loadComponents(std::launch type, const std::vector<deployment::Recipe> &recipes);
        std::shared_ptr<plugins::AbstractPlugin> loadComponent(const deployment::Recipe &recipe);
        size_t runLifecyclesToCompletion(std::vector<std::string> components);
        LifecycleResult runLifecycleStep(const std::string &name, const data::Symbol &phase);

        using ServiceMap =
            std::unordered_map<std::string, std::shared_ptr<plugins::AbstractPlugin>>;

        struct Services {
            [[nodiscard]] ServiceMap all() const {
                ServiceMap all = active;
                for(const auto &pair : inactive) {
                    all.emplace(pair);
                }
                for(const auto &pair : broken) {
                    all.emplace(pair);
                }
                return all;
            }
            ServiceMap active;
            ServiceMap inactive;
            ServiceMap broken;
        } _services;
        [[nodiscard]] ServiceMap getAllServices() const {
            std::unique_lock guard{_queueMutex};
            return _services.all();
        }

    public:
        std::future<bool> runComponents(std::vector<std::string>);
        std::future<bool> stopComponents(std::vector<std::string>);

        LifecycleManager(const scope::UsingContext &context, Kernel &kernel);
        LifecycleManager(const LifecycleManager &) = delete;
        LifecycleManager(LifecycleManager &&) = delete;
        LifecycleManager &operator=(const LifecycleManager &) = delete;
        LifecycleManager &operator=(LifecycleManager &&) = delete;

        ~LifecycleManager() noexcept;
    };

} // namespace lifecycle
