#pragma once
#include "data/handle_table.hpp"
#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>

namespace scope {
    class Context;
    class PerThreadContext;
} // namespace scope

namespace tasks {
    class ExpireTime;
    class Task;
    class TaskManager;

    /**
     * Base of all task threads strategies. This can be overridden to change behavior of a thread,
     * for example if it's a worker thread, or if it handles deferred tasks.
     */
    class TaskThread : public util::RefObject<TaskThread>, protected scope::UsesContext {

    protected:
        std::weak_ptr<scope::PerThreadContext> _threadContext;
        std::list<std::shared_ptr<Task>> _tasks;
        std::mutex _mutex;
        std::condition_variable _wake;
        std::atomic_bool _shutdown{false};

    public:
        explicit TaskThread(const scope::UsingContext &context);
        TaskThread(const TaskThread &) = delete;
        TaskThread(TaskThread &&) = delete;
        TaskThread &operator=(const TaskThread &) = delete;
        TaskThread &operator=(TaskThread &&) = delete;
        virtual ~TaskThread();
        void queueTask(const std::shared_ptr<Task> &task);
        std::shared_ptr<Task> pickupAffinitizedTask();
        std::shared_ptr<Task> pickupPoolTask();
        std::shared_ptr<Task> pickupTask();
        void bindThreadContext();
        void unbindThreadContext();
        std::shared_ptr<Task> getActiveTask() const;

        static std::shared_ptr<TaskThread> getThreadContext();

        void shutdown();

        virtual void join() {
        }
        virtual void setSingleThreadMode(bool) {
            // Override to do adhere to mode request
        }
        virtual bool isSingleThreadMode() {
            return false; // by default, mode is ignored
        }

        void stall(const ExpireTime &end);

        void waken();

        bool isShutdown();

        void taskStealing(const std::shared_ptr<Task> &blockedTask, const ExpireTime &end);
        void sleep(const ExpireTime &end);
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
        std::atomic_bool _running{false};

        void joinImpl();

    public:
        explicit TaskPoolWorker(const scope::UsingContext &context);
        TaskPoolWorker(const TaskPoolWorker &) = delete;
        TaskPoolWorker(TaskPoolWorker &&) = delete;
        TaskPoolWorker &operator=(const TaskPoolWorker &) = delete;
        TaskPoolWorker &operator=(TaskPoolWorker &&) = delete;

        ~TaskPoolWorker() override {
            joinImpl();
        }

        void start();

        void join() override {
            joinImpl();
        }

        void runner();
        static std::shared_ptr<TaskPoolWorker> create(const scope::UsingContext &context);
    };

    /**
     * Fixed threads that have no special behavior. This is the default thread assignment for
     * any thread that is not associated with any other task thread strategy.
     */
    class FixedTaskThread : public TaskThread {
    protected:
        data::ObjectAnchor _defaultTask;
        bool _singleThreadMode{false};

    public:
        explicit FixedTaskThread(const scope::UsingContext &context) : TaskThread(context) {
        }
        void setSingleThreadMode(bool f) override {
            _singleThreadMode = f;
        }
        bool isSingleThreadMode() override {
            return _singleThreadMode;
        }
    };

    //
    // Fixed thread that can do timer work - there's only one
    //
    class FixedTimerTaskThread : public FixedTaskThread {
    public:
        explicit FixedTimerTaskThread(const scope::UsingContext &context)
            : FixedTaskThread(context) {
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
                thread->bindThreadContext();
            }
        } // namespace tasks

        void claim(const std::shared_ptr<FixedTaskThread> &thread) {
            release();
            _thread = thread;
            if(thread) {
                thread->bindThreadContext();
            }
        }

        void release() {
            if(_thread) {
                _thread->unbindThreadContext();
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

    class BlockedThreadScope {
        const std::shared_ptr<Task> &_task;
        const std::shared_ptr<TaskThread> &_thread;

    public:
        BlockedThreadScope(const BlockedThreadScope &) = delete;
        BlockedThreadScope(BlockedThreadScope &&) = delete;
        BlockedThreadScope &operator=(const BlockedThreadScope &) = delete;
        BlockedThreadScope &operator=(BlockedThreadScope &&) = delete;

        explicit BlockedThreadScope(
            const std::shared_ptr<Task> &task, const std::shared_ptr<TaskThread> &thread);

        void taskStealing(const ExpireTime &end);

        ~BlockedThreadScope();
    };

} // namespace tasks
