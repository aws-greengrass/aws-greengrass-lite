#pragma once
#include "data/environment.hpp"
#include "data/handle_table.hpp"
#include "data/safe_handle.hpp"
#include "data/shared_struct.hpp"
#include "expire_time.hpp"
#include "pubsub/local_topics.hpp"
#include <condition_variable>
#include <list>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace tasks {
    class TaskManager;
    class TaskThread;
    class TaskPoolWorker;
    class Task;
    class TaskManager;

    class Task : public data::TrackingScope {
    public:
        enum Status { Running, NoSubTasks, Finalizing, SwitchThread, Completed };
        friend class TaskManager;

    private:
        std::shared_ptr<data::StructModelBase> _data;
        std::unique_ptr<SubTask> _finalize;
        std::list<std::unique_ptr<SubTask>> _subtasks;
        std::list<std::shared_ptr<TaskThread>> _blockedThreads;
        data::ObjHandle _self;
        ExpireTime _timeout;
        Status _lastStatus{Running};

        static data::ObjHandle getSetThreadSelf(data::ObjHandle h, bool set) {
            // This addresses a problem on (at least) Windows machines
            static thread_local uint32_t _threadTask{0};
            data::ObjHandle current{_threadTask};
            if(set) {
                _threadTask = h.asInt();
            }
            return current;
        }

    public:
        explicit Task(data::Environment &environment)
            : TrackingScope{environment}, _timeout{ExpireTime::fromNow(-1)} {
        }

        void setSelf(data::ObjHandle self) {
            std::unique_lock guard{_mutex};
            _self = self;
        }

        data::ObjHandle getSelf() {
            std::unique_lock guard{_mutex};
            return _self;
        }

        std::shared_ptr<data::StructModelBase> getData() {
            std::unique_lock guard{_mutex};
            return _data;
        }

        void setData(const std::shared_ptr<data::StructModelBase> &newData) {
            std::unique_lock guard{_mutex};
            _data = newData;
        }

        std::shared_ptr<TaskThread> getThreadAffinity();
        void markTaskComplete();

        static data::ObjHandle getThreadSelf() {
            return getSetThreadSelf({}, false);
        }

        static data::ObjHandle getSetThreadSelf(data::ObjHandle h) {
            return getSetThreadSelf(h, true);
        }

        Status removeSubtask(std::unique_ptr<SubTask> &subTask);
        void addSubtask(std::unique_ptr<SubTask> subTask);

        void setCompletion(std::unique_ptr<SubTask> finalize) {
            std::unique_lock guard{_mutex};
            _finalize = std::move(finalize);
        }

        void setTimeout(const ExpireTime &terminateTime) {
            std::unique_lock guard{_mutex};
            _timeout = terminateTime;
        }

        ExpireTime getTimeout() const {
            std::unique_lock guard{_mutex};
            return _timeout;
        }

        Status runInThread();
        bool waitForCompletion(const ExpireTime &terminateTime);
        Status runInThreadCallNext(
            const std::shared_ptr<Task> &task,
            const std::shared_ptr<data::StructModelBase> &dataIn,
            std::shared_ptr<data::StructModelBase> &dataOut
        );

        void addBlockedThread(const std::shared_ptr<TaskThread> &blockedThread);
        void removeBlockedThread(const std::shared_ptr<TaskThread> &blockedThread);

        bool isCompleted();
        bool willNeverComplete();

        Status finalizeTask(const std::shared_ptr<data::StructModelBase> &data);
    };

    class TaskThread : public util::RefObject<TaskThread> {
        // mix-in representing either a worker thread or fixed thread

    protected:
        data::Environment &_environment;
        std::weak_ptr<TaskManager> _pool;
        std::list<std::shared_ptr<Task>> _tasks;
        std::mutex _mutex;
        std::condition_variable _wake;
        bool _shutdown{false};
        void bindThreadContext();

        static TaskThread *getSetTaskThread(TaskThread *setValue, bool set) {
            // NOLINTNEXTLINE(*-avoid-non-const-global-variables)
            static thread_local TaskThread *_threadContext{nullptr};
            TaskThread *current = _threadContext;
            if(set) {
                _threadContext = setValue;
            }
            return current;
        }

    public:
        explicit TaskThread(
            data::Environment &environment, const std::shared_ptr<TaskManager> &pool
        );
        TaskThread(const TaskThread &) = delete;
        TaskThread(TaskThread &&) = delete;
        TaskThread &operator=(const TaskThread &) = delete;
        TaskThread &operator=(TaskThread &&) = delete;
        virtual ~TaskThread() = default;
        void queueTask(const std::shared_ptr<Task> &task);
        std::shared_ptr<Task> pickupAffinitizedTask();
        std::shared_ptr<Task> pickupPoolTask();
        std::shared_ptr<Task> pickupTask();
        virtual void releaseFixedThread();

        static std::shared_ptr<TaskThread> getThreadContext();

        void shutdown() {
            std::unique_lock guard(_mutex);
            _shutdown = true;
            _wake.notify_one();
        }

        void stall(const ExpireTime &end) {
            std::unique_lock guard(_mutex);
            if(_shutdown) {
                return;
            }
            _wake.wait_for(guard, end.remaining());
        }

        void waken() {
            std::unique_lock guard(_mutex);
            _wake.notify_one();
        }

        bool isShutdown() {
            std::unique_lock guard(_mutex);
            return _shutdown;
        }

        void taskStealing(const std::shared_ptr<Task> &blockingTask, const ExpireTime &end);

        std::shared_ptr<Task> pickupTask(const std::shared_ptr<Task> &blockingTask);

        std::shared_ptr<Task> pickupPoolTask(const std::shared_ptr<Task> &blockingTask);
    };

    class TaskPoolWorker : public TaskThread {
    private:
        std::thread _thread;

    public:
        explicit TaskPoolWorker(
            data::Environment &environment, const std::shared_ptr<TaskManager> &pool
        );
        void runner();
    };

    class FixedTaskThread : public TaskThread {
    protected:
        data::ObjectAnchor _defaultTask;
        std::shared_ptr<FixedTaskThread> _protectThread;

    public:
        explicit FixedTaskThread(
            data::Environment &environment, const std::shared_ptr<TaskManager> &pool
        )
            : TaskThread(environment, pool) {
        }

        // Call this on the native thread
        void bindThreadContext(const data::ObjectAnchor &task);
        void setDefaultTask(const data::ObjectAnchor &task);
        data::ObjectAnchor getDefaultTask();
        void protect();
        void unprotect();
        data::ObjectAnchor claimFixedThread();
        void releaseFixedThread() override;
    };

    inline void SubTask::setAffinity(const std::shared_ptr<TaskThread> &affinity) {
        _threadAffinity = affinity;
    }

    inline std::shared_ptr<TaskThread> SubTask::getAffinity() {
        return _threadAffinity;
    }

    class TaskManager : public data::TrackingScope {
    private:
        std::list<std::shared_ptr<TaskPoolWorker>> _busyWorkers; // assumes small
                                                                 // pool, else
                                                                 // std::set
        std::list<std::shared_ptr<TaskPoolWorker>> _idleWorkers; // LIFO
        std::list<std::shared_ptr<Task>> _backlog; // tasks with no thread affinity
                                                   // (assumed async)
        int _maxWorkers{5}; // TODO, from configuration

    public:
        explicit TaskManager(data::Environment &environment) : data::TrackingScope{environment} {
        }

        data::ObjectAnchor createTask();
        std::shared_ptr<Task> acquireTaskForWorker(TaskThread *worker);
        std::shared_ptr<Task> acquireTaskWhenStealing(
            TaskThread *worker, const std::shared_ptr<Task> &priorityTask
        );
        bool allocateNextWorker();
        void queueTask(const std::shared_ptr<Task> &task);
    };
} // namespace tasks
