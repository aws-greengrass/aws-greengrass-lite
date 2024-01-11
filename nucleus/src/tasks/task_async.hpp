#pragma once
#include "task.hpp"
#include "task_manager.hpp"

namespace tasks {

    template<typename Callable, typename... Args>
    class FunctionalAsyncSubTask : public tasks::SubTask {
    private:
        const Callable _callable;
        const std::tuple<Args...> _args;

    public:
        explicit FunctionalAsyncSubTask(Callable &&callable, Args &&...args)
            : _callable{std::move(callable)}, _args{std::forward<Args>(args)...} {
            static_assert(std::is_invocable_v<
                          Callable,
                          Args...,
                          const std::shared_ptr<data::StructModelBase>>);
        }

        std::shared_ptr<data::StructModelBase> runInThread(
            const std::shared_ptr<Task> &task,
            const std::shared_ptr<data::StructModelBase> &dataIn) override {

            auto args = std::tuple_cat(_args, std::tuple{dataIn});
            std::apply(_callable, args); // ignore return value, nothing will handle return data
            return {};
        }
    };

    template<typename Callable, typename... Args>
    std::shared_ptr<tasks::Task> TaskManager::defer(
        const std::shared_ptr<data::StructModelBase> &data,
        const tasks::ExpireTime &when,
        Callable &&func,
        Args &&...args) {
        auto subTask = std::make_unique<FunctionalAsyncSubTask<Callable, Args...>>(
            std::forward<Callable>(func), std::forward<Args>(args)...);
        auto taskObj = std::make_shared<tasks::Task>(context());
        taskObj->addSubtask(std::move(subTask));
        taskObj->setData(data);
        taskObj->setStartTime(when);
        queueTask(taskObj);
        return taskObj;
    }

    template<typename Callable, typename... Args>
    std::shared_ptr<tasks::Task> TaskManager::callAsync(Callable &&func, Args &&...args) {
        return defer(
            nullptr,
            ExpireTime::epoch(),
            std::forward<Callable>(func),
            std::forward<Args>(args)...);
    }
} // namespace tasks
