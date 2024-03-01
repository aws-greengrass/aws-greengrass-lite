#pragma once
#include "data/struct_model.hpp"
#include "expire_time.hpp"
#include "scope/context.hpp"
#include <atomic>
#include <list>

namespace pubsub {
    class FutureBase;
}

namespace tasks {
    class ExpireTime;
    class Task;
    class TaskPoolWorker;
    class TimerWorker;

    class TaskManager : protected scope::UsesContext {

        // A set of worker threads that are currently busy, assumed small
        std::list<std::unique_ptr<TaskPoolWorker>> _busyWorkers;
        // A set of idle worker threads, LIFO, assumed small
        std::list<std::unique_ptr<TaskPoolWorker>> _idleWorkers;
        // A worker occupied with timer activities
        std::unique_ptr<TimerWorker> _timerWorker;
        // A set of async callbacks that are looking for an idle worker
        std::list<std::shared_ptr<Task>> _backlog;
        // _delayedTasks is using multimap as an insertable ordered list,
        // TODO: is there a better std library for this?
        std::multimap<ExpireTime, std::shared_ptr<Task>> _delayedTasks;
        int64_t _maxWorkers{-1}; // TODO, from configuration, -1 = unbounded
        // thread decays - 'idle' number of threads have remained idle for at least 'decayMs' ms.
        int64_t _decayMs{1000}; // TODO, from configuration
        int64_t _decayIdle = 0;
        int64_t _minIdle =
            1; // TODO, from configuration - minimum number of threads to preserve from decay
        // If set, indicates that task manager is shutting down
        bool _shutdown{false};
        ExpireTime _nextDecayCheck = ExpireTime::now();

        std::shared_ptr<Task> acquireTaskForWorker(TaskPoolWorker *worker);
        bool allocateNextWorker();
        friend class TaskPoolWorker;

    protected:
        mutable std::mutex _mutex;

    public:
        explicit TaskManager(const scope::UsingContext &context);

        TaskManager(const TaskManager &) = delete;
        TaskManager(TaskManager &&) = delete;
        TaskManager &operator=(const TaskManager &) = delete;
        TaskManager &operator=(TaskManager &&) = delete;
        ~TaskManager();

        ExpireTime computeNextDeferredTask();
        ExpireTime computeIdleTaskDecay();
        void queueTask(const std::shared_ptr<Task> &task);
        void queueTask(const std::shared_ptr<Task> &task, const ExpireTime &when);
        void shutdownAndWait();
    };

} // namespace tasks
