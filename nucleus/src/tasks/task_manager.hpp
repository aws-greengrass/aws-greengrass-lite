#pragma once
#include "data/struct_model.hpp"
#include "scope/context.hpp"
#include <atomic>
#include <list>

namespace tasks {
    class ExpireTime;
    class Task;
    class TaskThread;
    class TaskPoolWorker;

    class TaskManager : protected scope::UsesContext {
        // Root of all active tasks, an active task is assumed to eventually terminate
        std::shared_ptr<data::TrackingRoot> _root;
        // A set of worker threads that are currently busy, assumed small
        std::list<std::shared_ptr<TaskPoolWorker>> _busyWorkers;
        // A set of idle worker threads, LIFO, assumed small
        std::list<std::shared_ptr<TaskPoolWorker>> _idleWorkers;
        // A set of async tasks that are looking for an idle worker
        std::list<std::shared_ptr<Task>> _backlog;
        // The thread that is handling time-based tasks
        std::weak_ptr<TaskThread> _timerWorkerThread;
        // _delayedTasks is using multimap as an insertable ordered list,
        // TODO: is there a better std library for this?
        std::multimap<ExpireTime, std::shared_ptr<Task>> _delayedTasks;
        uint32_t _maxWorkers{5}; // TODO, from configuration
        // Indicates that task manager is shutting down
        std::atomic_bool _shutdown{false};

        void scheduleFutureTaskAssumeLocked(
            const ExpireTime &when, const std::shared_ptr<Task> &task);
        void descheduleFutureTaskAssumeLocked(
            const ExpireTime &when, const std::shared_ptr<Task> &task);
        std::shared_ptr<Task> acquireTaskForWorker(TaskThread *worker);
        std::shared_ptr<Task> acquireTaskWhenStealing(const std::shared_ptr<Task> &priorityTask);
        bool allocateNextWorker();
        void cancelWaitingTasks();
        void shutdownAllWorkers(bool join);
        friend class Task;
        friend class TaskThread;

    protected:
        mutable std::mutex _mutex;

    public:
        explicit TaskManager(const scope::UsingContext &context)
            : scope::UsesContext(context), _root(std::make_shared<data::TrackingRoot>(context)) {
        }

        TaskManager(const TaskManager &) = delete;
        TaskManager(TaskManager &&) = delete;
        TaskManager &operator=(const TaskManager &) = delete;
        TaskManager &operator=(TaskManager &&) = delete;
        ~TaskManager();

        void queueTask(const std::shared_ptr<Task> &task);
        void resumeTask(const std::shared_ptr<Task> &task);
        ExpireTime pollNextDeferredTask(TaskThread *worker);
        void shutdownAndWait();
    };

} // namespace tasks
