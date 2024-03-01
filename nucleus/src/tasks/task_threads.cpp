#include "task_threads.hpp"
#include "expire_time.hpp"
#include "scope/context_full.hpp"
#include "task.hpp"

namespace tasks {

    void TaskPoolWorker::bindThreadContext() {
        auto tc = scope::thread();
        tc->changeContext(context());
    }

    TaskPoolWorker::TaskPoolWorker(const scope::UsingContext &context)
        : scope::UsesContext(context) {
    }

    std::unique_ptr<TaskPoolWorker> TaskPoolWorker::create(const scope::UsingContext &context) {

        auto worker = std::make_unique<TaskPoolWorker>(context);
        worker->start();
        return worker;
    }

    std::unique_ptr<TimerWorker> TimerWorker::create(const scope::UsingContext &context) {

        auto worker = std::make_unique<TimerWorker>(context);
        worker->start();
        return worker;
    }

    void TaskPoolWorker::start() {
        // Do not call in constructor, see notes in runner()
        if(!_running.exchange(true)) {
            // Max one start
            _thread = std::thread(&TaskPoolWorker::runner, this);
        }
    }

    void TaskPoolWorker::runner() {
        bindThreadContext(); // TaskPoolWorker must be fully initialized before this
        while(!isShutdown()) {
            runLoop();
        }
    }

    void TaskPoolWorker::runLoop() {
        std::shared_ptr<Task> task = pickupTask();
        if(task) {
            task->invoke();
        } else {
            stall(ExpireTime::infinite());
        }
    }

    void TimerWorker::runLoop() {
        auto ctx = context();
        if(!ctx) {
            return;
        }
        auto &pool = ctx->taskManager();
        ExpireTime nextTime = pool.computeNextDeferredTask();
        ExpireTime nextDecayTime = pool.computeIdleTaskDecay();
        if(nextTime > nextDecayTime) {
            nextTime = nextDecayTime;
        }
        if(nextTime != ExpireTime::unspecified()) {
            stall(nextTime);
        }
    }

    void TaskPoolWorker::join() {
        std::unique_lock guard{_mutex};
        _shutdown = true;
        _wake.notify_one();
        if(_running.exchange(false)) {
            guard.unlock();
            // Max one join
            _thread.join();
        }
    }

    std::shared_ptr<Task> TaskPoolWorker::pickupTask() {
        auto ctx = context();
        if(!ctx) {
            return {};
        }
        auto &pool = ctx->taskManager();
        return pool.acquireTaskForWorker(this);
    }

    void TaskPoolWorker::shutdown() {
        std::unique_lock guard(_mutex);
        _shutdown = true;
        _wake.notify_one();
    }

    void TaskPoolWorker::stall(const ExpireTime &end) {
        std::unique_lock guard(_mutex);
        if(_shutdown) {
            return;
        }
        _wake.wait_until(guard, end.toTimePoint());
    }

    void TaskPoolWorker::waken() {
        std::unique_lock guard(_mutex);
        _wake.notify_one();
    }

    bool TaskPoolWorker::isShutdown() {
        return _shutdown.load();
    }
} // namespace tasks
