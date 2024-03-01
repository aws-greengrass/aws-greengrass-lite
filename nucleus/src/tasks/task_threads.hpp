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
        bool isShutdown();
        std::shared_ptr<Task> pickupTask();
        void stall(const ExpireTime &expireTime);

    public:
        explicit TaskPoolWorker(const scope::UsingContext &context);
        TaskPoolWorker(const TaskPoolWorker &) = delete;
        TaskPoolWorker(TaskPoolWorker &&) = delete;
        TaskPoolWorker &operator=(const TaskPoolWorker &) = delete;
        TaskPoolWorker &operator=(TaskPoolWorker &&) = delete;

        virtual ~TaskPoolWorker() {
            join();
        }

        void start();
        void shutdown();
        void runner();
        virtual void runLoop();
        void join();
        void waken();
        static std::unique_ptr<TaskPoolWorker> create(const scope::UsingContext &context);
    };

    class TimerWorker : public TaskPoolWorker {
    public:
        explicit TimerWorker(const scope::UsingContext &context) : TaskPoolWorker(context) {
        }
        void runLoop() override;
        static std::unique_ptr<TimerWorker> create(const scope::UsingContext &context);
    };

} // namespace tasks
