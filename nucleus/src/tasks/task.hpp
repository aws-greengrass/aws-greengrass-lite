#pragma once
#include "data/handle_table.hpp"
#include "data/struct_model.hpp"
#include "scope/call_scope.hpp"
#include "tasks/expire_time.hpp"
#include <chrono>
#include <list>

namespace tasks {
    class Callback;
    class SubTask;
    class TaskManager;
    class TaskThread;
    class Task;
    class TaskManager;

    class Task : public data::TrackedObject {
    public:
        enum Status {
            Pending,
            Running,
            NoSubTasks,
            HasReturnValue,
            Finalizing,
            SwitchThread,
            Completed,
            Cancelled
        };
        friend class TaskManager;

    protected:
        void releaseBlockedThreads(bool isLocked);
        void signalBlockedThreads(bool isLocked);
        bool queueTaskInterlockedTrySetRunning();

        scope::FixedPtr<TaskManager> getTaskManager() {
            std::shared_ptr<scope::Context> ctx = _context.lock();
            if(ctx) {
                return scope::FixedPtr<TaskManager>::of(&ctx->taskManager());
            } else {
                return {};
            }
        }

    private:
        mutable std::shared_mutex _mutex;
        std::shared_ptr<data::StructModelBase> _data;
        std::unique_ptr<SubTask> _finalize;
        std::list<std::unique_ptr<SubTask>> _subtasks;
        std::list<std::shared_ptr<TaskThread>> _blockedThreads;
        std::shared_ptr<TaskThread> _defaultThread;
        data::ObjHandle _self;
        ExpireTime _timeout{ExpireTime::infinite()}; // time before task is automatically cancelled
        ExpireTime _start{ExpireTime::unspecified()}; // desired start time (default is immediately)
        Status _lastStatus{Pending};

    public:
        explicit Task(const std::shared_ptr<scope::Context> &context)
            : data::TrackedObject(context) {
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

        void setDefaultThread(const std::shared_ptr<TaskThread> &thread);
        std::shared_ptr<TaskThread> getThreadAffinity();
        void markTaskComplete();
        void cancelTask();

        Status removeSubtask(std::unique_ptr<SubTask> &subTask);
        void addSubtask(std::unique_ptr<SubTask> subTask);

        void setCompletion(std::unique_ptr<SubTask> finalize) {
            std::unique_lock guard{_mutex};
            _finalize = std::move(finalize);
        }

        void setTimeout(const ExpireTime &terminateTime);
        ExpireTime getTimeout() const;
        bool setStartTime(const ExpireTime &terminateTime);
        ExpireTime getStartTime() const;

        ExpireTime getEffectiveTimeout(const ExpireTime &terminalTime) const {
            std::unique_lock guard{_mutex};
            if(terminalTime < _timeout) {
                return terminalTime;
            } else {
                return _timeout;
            }
        }

        ExpireTime getTimeout(const ExpireTime &terminalTime) const {
            std::unique_lock guard{_mutex};
            if(_timeout == ExpireTime::infinite() || terminalTime < _timeout) {
                return terminalTime;
            } else {
                return _timeout;
            }
        }

        Status runInThread();
        bool waitForCompletion(const ExpireTime &terminateTime);

        template<class Rep, class Period>
        inline bool waitForCompletionDelta(const std::chrono::duration<Rep, Period> &delta) {
            return waitForCompletion(ExpireTime::fromNow(delta));
        }

        // Waits indefinitely for task completion.
        // May return early with false if wait is terminated externally.
        inline bool wait() {
            return waitForCompletion(ExpireTime::infinite());
        }

        Status runInThreadCallNext(
            const std::shared_ptr<Task> &task,
            const std::shared_ptr<data::StructModelBase> &dataIn,
            std::shared_ptr<data::StructModelBase> &dataOut);

        void addBlockedThread(const std::shared_ptr<TaskThread> &blockedThread);
        void removeBlockedThread(const std::shared_ptr<TaskThread> &blockedThread);

        bool isCompleted();
        bool terminatesWait();

        Status finalizeTask(const std::shared_ptr<data::StructModelBase> &data);
        void requeueTask();

        void beforeRemove(const data::ObjectAnchor &anchor) override;
    };

    // Ensures thread data contains information about current task, and
    // performs RII cleanup
    class CurrentTaskScope {
    private:
        std::shared_ptr<Task> _oldTask;
        std::shared_ptr<Task> _activeTask;

    public:
        explicit CurrentTaskScope(const std::shared_ptr<Task> &activeTask);
        CurrentTaskScope(const CurrentTaskScope &) = delete;
        CurrentTaskScope(CurrentTaskScope &&) = delete;
        CurrentTaskScope &operator=(const CurrentTaskScope &) = delete;
        CurrentTaskScope &operator=(CurrentTaskScope &&) = delete;
        ~CurrentTaskScope();
    };
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
            const std::shared_ptr<Task> &task,
            const std::shared_ptr<data::StructModelBase> &dataIn) = 0;
        void setAffinity(const std::shared_ptr<TaskThread> &affinity);
        std::shared_ptr<TaskThread> getAffinity(const std::shared_ptr<TaskThread> &defaultThread);
    };

    class SimpleSubTask : public tasks::SubTask {
    private:
        std::shared_ptr<tasks::Callback> _callback;

    public:
        explicit SimpleSubTask(const std::shared_ptr<tasks::Callback> &callback)
            : _callback{callback} {
        }

        std::shared_ptr<data::StructModelBase> runInThread(
            const std::shared_ptr<tasks::Task> &task,
            const std::shared_ptr<data::StructModelBase> &data) override;
    };

} // namespace tasks
