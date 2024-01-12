#include "task_threads.hpp"
#include "expire_time.hpp"
#include "scope/context_full.hpp"
#include "task.hpp"
#include <iostream>

namespace tasks {

    void TaskThread::queueTask(const std::shared_ptr<Task> &task) {
        std::unique_lock guard{_mutex};
        if(_shutdown) {
            task->cancelTask();
        } else {
            _tasks.push_back(task);
        }
    }

    void TaskThread::bindThreadContext() {
        auto tc = scope::PerThreadContext::get();
        _threadContext = tc;
        tc->changeContext(context());
        tc->setThreadTaskData(baseRef());
    }

    std::shared_ptr<TaskThread> TaskThread::getThreadContext() {
        return scope::thread()->getThreadTaskData();
    }

    TaskThread::TaskThread(const scope::UsingContext &context) : scope::UsesContext(context) {
    }

    TaskPoolWorker::TaskPoolWorker(const scope::UsingContext &context) : TaskThread(context) {
    }

    std::shared_ptr<TaskPoolWorker> TaskPoolWorker::create(const scope::UsingContext &context) {

        std::shared_ptr<TaskPoolWorker> worker{std::make_shared<TaskPoolWorker>(context)};
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
            std::shared_ptr<Task> task = pickupTask();
            if(task) {
                task->runInThread();
            } else {
                stall(ExpireTime::infinite());
            }
        }
    }

    void TaskPoolWorker::joinImpl() {
        std::unique_lock guard{_mutex};
        _shutdown = true;
        _wake.notify_one();
        if(_running.exchange(false)) {
            guard.unlock();
            // Max one join
            _thread.join();
        }
    }

    void TaskThread::taskStealing(const std::shared_ptr<Task> &blockedTask, const ExpireTime &end) {
        while(!(blockedTask->terminatesWait() || isShutdown())) {
            ExpireTime adjEnd = blockedTask->getEffectiveTimeout(end); // Note: timeout may change
            adjEnd = taskStealingHook(adjEnd);
            std::shared_ptr<Task> task = pickupTask(blockedTask);
            if(task) {
                task->runInThread();
            } else {
                if(adjEnd <= ExpireTime::now()) {
                    return;
                }
                stall(adjEnd);
            }
        }
    }

    void TaskThread::sleep(const ExpireTime &end) {
        while(!isShutdown()) {
            ExpireTime adjEnd = taskStealingHook(end);
            std::shared_ptr<Task> task = pickupTask();
            if(task) {
                task->runInThread();
            } else {
                if(adjEnd <= ExpireTime::now()) {
                    return;
                }
                stall(adjEnd);
            }
        }
    }

    std::shared_ptr<Task> TaskThread::pickupAffinitizedTask() {
        std::unique_lock guard{_mutex};
        if(_tasks.empty()) {
            // shutdown case handled outside of this
            return {};
        }
        std::shared_ptr<Task> next = _tasks.front();
        _tasks.pop_front();
        return next;
    }

    std::shared_ptr<Task> TaskThread::pickupPoolTask(const std::shared_ptr<Task> &blockingTask) {
        if(isShutdown()) {
            return {}; // Bail quickly if we already know we're in shutdown
        }
        // TODO: On some versions of GCC the Context destructor is called here, not sure why...
        auto ctx = context();
        if(!ctx || isShutdown()) {
            return {};
        }
        auto &pool = ctx->taskManager();
        return pool.acquireTaskWhenStealing(blockingTask);
    }

    std::shared_ptr<Task> TaskThread::pickupPoolTask() {
        if(isShutdown()) {
            return {}; // Bail quickly if we already know we're in shutdown
        }
        // TODO: On some versions of GCC the Context destructor is called here, not sure why...
        auto ctx = context();
        if(!ctx || isShutdown()) {
            return {};
        }
        auto &pool = ctx->taskManager();
        return pool.acquireTaskForWorker(this);
    }

    std::shared_ptr<Task> TaskThread::pickupTask() {
        std::shared_ptr<Task> task{pickupAffinitizedTask()};
        if(task) {
            return task;
        }
        return pickupPoolTask();
    }

    std::shared_ptr<Task> TaskThread::pickupTask(const std::shared_ptr<Task> &blockingTask) {
        std::shared_ptr<Task> task{pickupAffinitizedTask()};
        if(task) {
            return task;
        }
        return pickupPoolTask(blockingTask);
    }

    std::shared_ptr<Task> TaskThread::getActiveTask() const {
        std::shared_ptr<scope::PerThreadContext> tc = _threadContext.lock();
        if(tc) {
            return tc->getActiveTask();
        } else {
            return {};
        }
    }

    void TaskThread::unbindThreadContext() {
        std::shared_ptr<scope::PerThreadContext> tc = _threadContext.lock();
        if(!tc) {
            return;
        }
        auto prev = tc->getThreadTaskData();
        if(prev && prev.get() == this) {
            tc->setThreadTaskData({});
            tc->setActiveTask({});
        } else {
            throw std::runtime_error("Unable to unbind thread");
        }
    }

    ExpireTime TaskThread::taskStealingHook(ExpireTime time) {
        return time;
    }

    ExpireTime FixedTimerTaskThread::taskStealingHook(ExpireTime time) {
        auto ctx = context();
        if(!ctx || isShutdown()) {
            return time;
        }
        auto &taskManager = ctx->taskManager();
        ExpireTime nextDeferredTime = taskManager.pollNextDeferredTask(this);
        if(nextDeferredTime == ExpireTime::infinite() || time < nextDeferredTime) {
            return time;
        } else {
            return nextDeferredTime;
        }
    }

    TaskThread::~TaskThread() {
        disposeWaitingTasks();
    }

    void TaskThread::disposeWaitingTasks() {
        std::unique_lock guard{_mutex};
        std::vector<std::shared_ptr<Task>> tasks;
        _shutdown = true;
        for(auto &task : _tasks) {
            tasks.emplace_back(task);
        }
        _tasks.clear();
        guard.unlock();
        // Anything with a thread affinity must necessarily be cancelled
        for(auto &task : tasks) {
            task->cancelTask();
        }
    }

    void TaskThread::shutdown() {
        std::unique_lock guard(_mutex);
        _shutdown = true;
        _wake.notify_one();
    }

    void TaskThread::stall(const ExpireTime &end) {
        std::unique_lock guard(_mutex);
        if(_shutdown) {
            return;
        }
        _wake.wait_until(guard, end.toTimePoint());
    }

    void TaskThread::waken() {
        std::unique_lock guard(_mutex);
        _wake.notify_one();
    }

    bool TaskThread::isShutdown() {
        return _shutdown.load();
    }

    std::shared_ptr<Task> FixedTaskThreadScope::getTask() const {
        return _thread->getActiveTask();
    }

    BlockedThreadScope::BlockedThreadScope(
        const std::shared_ptr<Task> &task, const std::shared_ptr<TaskThread> &thread)
        : _task(task), _thread(thread) {
        _task->addBlockedThread(_thread);
    }

    BlockedThreadScope::~BlockedThreadScope() {
        _task->removeBlockedThread(_thread);
    }

    void BlockedThreadScope::taskStealing(const ExpireTime &end) {
        _thread->taskStealing(_task, end);
    }
} // namespace tasks
