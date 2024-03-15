#pragma once
#include "data/handle_table.hpp"
#include "data/struct_model.hpp"
#include "tasks/expire_time.hpp"
#include <chrono>
#include <list>

namespace tasks {
    class Callback;
    class TaskManager;
    class Task;
    class TaskManager;

    class Task {
    protected:
    public:
        Task() = default;
        Task(const Task &) = delete;
        Task(Task &&) = delete;
        Task &operator=(const Task &) = delete;
        Task &operator=(Task &&) = delete;
        virtual ~Task() = default;
        virtual void invoke() = 0;
    };

    class AsyncCallbackTask : public Task {
        const std::shared_ptr<Callback> _callback;

    public:
        AsyncCallbackTask(const std::shared_ptr<tasks::Callback> &callback) : _callback(callback) {
        }
        void invoke() override;
    };

} // namespace tasks
