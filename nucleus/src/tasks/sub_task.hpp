#pragma once
#include "data/struct_model.hpp"
#include "expire_time.hpp"

namespace tasks {
    class TaskThread;
    class Task;

    class SubTask : public util::RefObject<SubTask> {
    protected:
        std::shared_ptr<TaskThread> _threadAffinity;

    public:
        SubTask() = default;
        SubTask(const SubTask &) = delete;
        SubTask(SubTask &&) = delete;
        SubTask &operator=(const SubTask &) = delete;
        SubTask &operator=(SubTask &&) = delete;
        virtual ~SubTask() = default;
        virtual std::shared_ptr<data::StructModelBase> runInThread(
            const std::shared_ptr<Task> &task, const std::shared_ptr<data::StructModelBase> &dataIn
        ) = 0;
        void setAffinity(const std::shared_ptr<TaskThread> &affinity);
        std::shared_ptr<TaskThread> getAffinity(const std::shared_ptr<TaskThread> &defaultThread);
    };
} // namespace tasks
