#pragma once
#include "data/struct_model.hpp"
#include <atomic>
#include <list>

namespace tasks {
    class ExpireTime;
    class Task;
    class TaskThread;
    class TaskPoolWorker;

    class TaskManager : public data::TrackingScope {
        std::list<std::shared_ptr<TaskPoolWorker>> _busyWorkers; // assumes small
                                                                 // pool, else
                                                                 // std::set
        std::list<std::shared_ptr<TaskPoolWorker>> _idleWorkers; // LIFO
        std::list<std::shared_ptr<Task>> _backlog; // tasks with no thread affinity
                                                   // (assumed async)
        std::weak_ptr<TaskThread> _timerWorkerThread;
        // _delayedTasks is using multimap as an insertable ordered list,
        // TODO: is there a better std library for this?
        std::multimap<ExpireTime, std::shared_ptr<Task>> _delayedTasks;
        int _maxWorkers{5}; // TODO, from configuration
        std::atomic_bool _shutdown{false};

        void scheduleFutureTaskAssumeLocked(
            const ExpireTime &when, const std::shared_ptr<Task> &task
        );
        void descheduleFutureTaskAssumeLocked(
            const ExpireTime &when, const std::shared_ptr<Task> &task
        );
        std::shared_ptr<Task> acquireTaskForWorker(TaskThread *worker);
        std::shared_ptr<Task> acquireTaskWhenStealing(
            TaskThread *worker, const std::shared_ptr<Task> &priorityTask
        );
        bool allocateNextWorker();
        void cancelWaitingTasks();
        void shutdownAllWorkers(bool join);
        friend class Task;
        friend class TaskThread;

    public:
        explicit TaskManager(data::Environment &environment) : data::TrackingScope{environment} {
        }

        TaskManager(const TaskManager &) = delete;
        TaskManager(TaskManager &&) = delete;
        TaskManager &operator=(const TaskManager &) = delete;
        TaskManager &operator=(TaskManager &&) = delete;
        ~TaskManager() override;

        data::ObjectAnchor createTask();
        void queueTask(const std::shared_ptr<Task> &task);
        void resumeTask(const std::shared_ptr<Task> &task);
        ExpireTime pollNextDeferredTask(TaskThread *worker);
        void shutdownAndWait();
    };

    class TaskManagerContainer {
        std::shared_ptr<TaskManager> _mgr;

    public:
        explicit TaskManagerContainer(data::Environment &env)
            : _mgr{std::make_shared<tasks::TaskManager>(env)} {
        }

        TaskManager *operator->() {
            return _mgr.get();
        }

        // NOLINTNEXTLINE(*-explicit-constructor)
        operator std::shared_ptr<TaskManager>() {
            return _mgr;
        }

        ~TaskManagerContainer() {
            if(_mgr) {
                _mgr->shutdownAndWait();
                _mgr.reset();
            }
        }
    };

} // namespace tasks
