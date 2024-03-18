#pragma once
#include "data/handle_table.hpp"
#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>

namespace pubsub {
    class FutureBase;
}

namespace tasks {
    class ExpireTime;
    class TaskManager;
    class Task;

    /**
     * Dynamic worker threads
     */
    class TaskPoolWorker : protected scope::UsesContext {
    private:
        std::thread _thread;
        std::atomic_bool _running{false};
        std::mutex _mutex;
        std::condition_variable _wake;
        std::atomic_bool _shutdown{false};

    private:
        void bindThreadContext();

    protected:
        bool isShutdown() noexcept;
        std::shared_ptr<Task> pickupTask();
        void stall(const ExpireTime &expireTime) noexcept;

    public:
        explicit TaskPoolWorker(const scope::UsingContext &context);
        TaskPoolWorker(const TaskPoolWorker &) = delete;
        TaskPoolWorker(TaskPoolWorker &&) = delete;
        TaskPoolWorker &operator=(const TaskPoolWorker &) = delete;
        TaskPoolWorker &operator=(TaskPoolWorker &&) = delete;

        virtual ~TaskPoolWorker() noexcept {
            join();
        }

        void start();
        void shutdown() noexcept;
        void runner();
        virtual void runLoop() noexcept;
        void join() noexcept;
        void waken() noexcept;
        static std::unique_ptr<TaskPoolWorker> create(const scope::UsingContext &context);
    };

    class TimerWorker : public TaskPoolWorker {
    public:
        explicit TimerWorker(const scope::UsingContext &context) : TaskPoolWorker(context) {
        }
        void runLoop() noexcept override;
        static std::unique_ptr<TimerWorker> create(const scope::UsingContext &context);
    };

} // namespace tasks
