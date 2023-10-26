#pragma once
#include "data/handle_table.hpp"
#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>

namespace tasks {
    class ExpireTime;
    class Task;
    class TaskManager;

    //
    // Base of all task threads, workers and fixed
    //
    class TaskThread : public util::RefObject<TaskThread> {

    protected:
        data::Environment &_environment;
        std::weak_ptr<TaskManager> _pool;
        std::list<std::shared_ptr<Task>> _tasks;
        std::mutex _mutex;
        std::condition_variable _wake;
        bool _shutdown{false};
        void bindThreadContext();

        static TaskThread *getSetTaskThread(TaskThread *setValue, bool set);

    public:
        explicit TaskThread(
            data::Environment &environment, const std::shared_ptr<TaskManager> &pool
        );
        TaskThread(const TaskThread &) = delete;
        TaskThread(TaskThread &&) = delete;
        TaskThread &operator=(const TaskThread &) = delete;
        TaskThread &operator=(TaskThread &&) = delete;
        virtual ~TaskThread();
        void queueTask(const std::shared_ptr<Task> &task);
        std::shared_ptr<Task> pickupAffinitizedTask();
        std::shared_ptr<Task> pickupPoolTask();
        std::shared_ptr<Task> pickupTask();
        virtual void releaseFixedThread();

        static std::shared_ptr<TaskThread> getThreadContext();

        void shutdown();

        virtual void join() {
        }

        void stall(const ExpireTime &end);

        void waken();

        bool isShutdown();

        void taskStealing(const std::shared_ptr<Task> &blockedTask, const ExpireTime &end);
        virtual ExpireTime taskStealingHook(ExpireTime time);

        std::shared_ptr<Task> pickupTask(const std::shared_ptr<Task> &blockingTask);

        std::shared_ptr<Task> pickupPoolTask(const std::shared_ptr<Task> &blockingTask);
        void disposeWaitingTasks();
    };

    //
    // Dynamic worker threads
    //
    class TaskPoolWorker : public TaskThread {
    private:
        std::thread _thread;

        void joinImpl();

    public:
        explicit TaskPoolWorker(
            data::Environment &environment, const std::shared_ptr<TaskManager> &pool
        );
        TaskPoolWorker(const TaskPoolWorker &) = delete;
        TaskPoolWorker(TaskPoolWorker &&) = delete;
        TaskPoolWorker &operator=(const TaskPoolWorker &) = delete;
        TaskPoolWorker &operator=(TaskPoolWorker &&) = delete;

        ~TaskPoolWorker() override {
            joinImpl();
        }

        void join() override {
            joinImpl();
        }

        void runner();
    };

    //
    // Fixed threads that never go away
    //
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

    //
    // Fixed thread that can do timer work - there's only one
    //
    class FixedTimerTaskThread : public FixedTaskThread {
    public:
        explicit FixedTimerTaskThread(
            data::Environment &environment, const std::shared_ptr<TaskManager> &pool
        )
            : FixedTaskThread(environment, pool) {
        }

        ExpireTime taskStealingHook(ExpireTime time) override;
    };

    class FixedTaskThreadScope {
        std::shared_ptr<FixedTaskThread> _thread;

    public:
        FixedTaskThreadScope() = default;
        FixedTaskThreadScope(const FixedTaskThreadScope &) = delete;
        FixedTaskThreadScope &operator=(const FixedTaskThreadScope &) = delete;
        FixedTaskThreadScope(FixedTaskThreadScope &&) noexcept = default;
        FixedTaskThreadScope &operator=(FixedTaskThreadScope &&) = default;

        ~FixedTaskThreadScope() {
            release();
        }

        explicit FixedTaskThreadScope(const std::shared_ptr<FixedTaskThread> &thread)
            : _thread(thread) {
            if(thread) {
                thread->claimFixedThread();
            }
        } // namespace tasks

        void claim(const std::shared_ptr<FixedTaskThread> &thread) {
            release();
            _thread = thread;
            if(thread) {
                thread->claimFixedThread();
            }
        }

        void release() {
            if(_thread) {
                _thread->releaseFixedThread();
                _thread.reset();
            }
        }

        [[nodiscard]] std::shared_ptr<FixedTaskThread> get() const {
            return _thread;
        }

        [[nodiscard]] std::shared_ptr<Task> getTask() const;

        explicit operator bool() {
            return _thread.operator bool();
        }

        bool operator!() {
            return !_thread;
        }
    };

    // Ensures thread-self is assigned and cleaned up with stack unwinding (RII)
    class ThreadSelf {
    private:
        data::ObjHandle _oldHandle;

    public:
        ThreadSelf(const ThreadSelf &) = delete;
        ThreadSelf(ThreadSelf &&) = delete;
        ThreadSelf &operator=(const ThreadSelf &) = delete;
        ThreadSelf &operator=(ThreadSelf &&) = delete;

        explicit ThreadSelf(Task *contextTask);

        ~ThreadSelf();
    };

    class BlockedThreadScope {
        const std::shared_ptr<Task> &_task;
        const std::shared_ptr<TaskThread> &_thread;

    public:
        BlockedThreadScope(const BlockedThreadScope &) = delete;
        BlockedThreadScope(BlockedThreadScope &&) = delete;
        BlockedThreadScope &operator=(const BlockedThreadScope &) = delete;
        BlockedThreadScope &operator=(BlockedThreadScope &&) = delete;

        explicit BlockedThreadScope(
            const std::shared_ptr<Task> &task, const std::shared_ptr<TaskThread> &thread
        );

        void taskStealing(const ExpireTime &end);

        ~BlockedThreadScope();
    };

} // namespace tasks
