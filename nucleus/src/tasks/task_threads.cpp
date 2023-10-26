#include "task_threads.hpp"
#include "expire_time.hpp"
#include "task.hpp"
#include "task_manager.hpp"
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
        getSetTaskThread(this, true);
    }

    std::shared_ptr<TaskThread> TaskThread::getThreadContext() {
        // TaskThread for current thread
        TaskThread *thread = getSetTaskThread(nullptr, false);
        if(thread) {
            return thread->shared_from_this();
        } else {
            return nullptr;
        }
    }

    void FixedTaskThread::setDefaultTask(const data::ObjectAnchor &task) {
        std::unique_lock guard{_mutex};
        _defaultTask = task;
    }

    data::ObjectAnchor FixedTaskThread::getDefaultTask() {
        std::unique_lock guard{_mutex};
        return _defaultTask;
    }

    void FixedTaskThread::protect() {
        std::unique_lock guard{_mutex};
        _protectThread = ref<FixedTaskThread>();
    }

    void FixedTaskThread::unprotect() {
        std::unique_lock guard{_mutex};
        _protectThread = nullptr;
    }

    void FixedTaskThread::bindThreadContext(const data::ObjectAnchor &task) {
        // Run on target thread
        setDefaultTask(task);
        task.getObject<Task>()->getSetThreadSelf(task.getHandle());
        TaskThread::bindThreadContext();
    }

    TaskThread::TaskThread(data::Environment &environment, const std::shared_ptr<TaskManager> &pool)
        : _environment{environment}, _pool{pool} {
    }

    TaskPoolWorker::TaskPoolWorker(
        data::Environment &environment, const std::shared_ptr<TaskManager> &pool
    )
        : TaskThread(environment, pool) {
        _thread = std::thread(&TaskPoolWorker::runner, this);
    }

    void TaskPoolWorker::runner() {
        bindThreadContext();
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
        shutdown();
        _wake.notify_one();
        if(_thread.joinable()) {
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
        std::shared_ptr<TaskManager> pool = _pool.lock();
        if(!pool || isShutdown()) {
            return {};
        }
        return pool->acquireTaskWhenStealing(this, blockingTask);
    }

    std::shared_ptr<Task> TaskThread::pickupPoolTask() {
        std::shared_ptr<TaskManager> pool = _pool.lock();
        if(!pool || isShutdown()) {
            return {};
        }
        return pool->acquireTaskForWorker(this);
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

    data::ObjectAnchor FixedTaskThread::claimFixedThread() {
        std::shared_ptr<TaskManager> mgr{_pool.lock()};
        data::ObjectAnchor taskAnchor{mgr->createTask()};
        bindThreadContext(taskAnchor);
        protect();
        return taskAnchor;
    }

    void TaskThread::releaseFixedThread() {
        throw std::runtime_error("Releasing a non-fixed thread");
    }

    ExpireTime TaskThread::taskStealingHook(ExpireTime time) {
        return time;
    }

    void FixedTaskThread::releaseFixedThread() {
        data::ObjectAnchor defaultTask = FixedTaskThread::getDefaultTask();
        data::ObjHandle taskHandle{Task::getThreadSelf()};
        if((!defaultTask) || defaultTask.getHandle() != taskHandle) {
            throw std::runtime_error("Thread associated with another task");
        }
        setDefaultTask({});
        defaultTask.release();
        Task::getSetThreadSelf({});
        unprotect();
        getSetTaskThread(nullptr, true);
    }

    ExpireTime FixedTimerTaskThread::taskStealingHook(ExpireTime time) {
        std::shared_ptr<TaskManager> taskManager{_pool.lock()};
        if(!taskManager) {
            return time;
        }
        ExpireTime nextDeferredTime = taskManager->pollNextDeferredTask(this);
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

    TaskThread *TaskThread::getSetTaskThread(TaskThread *setValue, bool set) {
        // NOLINTNEXTLINE(*-avoid-non-const-global-variables)
        static thread_local TaskThread *_threadContext{nullptr};
        TaskThread *current = _threadContext;
        if(set) {
            _threadContext = setValue;
        }
        return current;
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
        std::unique_lock guard(_mutex);
        return _shutdown;
    }

    std::shared_ptr<Task> FixedTaskThreadScope::getTask() const {
        return _thread->getDefaultTask().getObject<tasks::Task>();
    }

    ThreadSelf::ThreadSelf(Task *contextTask) {
        _oldHandle = Task::getSetThreadSelf(contextTask->getSelf());
    }

    ThreadSelf::~ThreadSelf() {
        Task::getSetThreadSelf(_oldHandle);
    }

    BlockedThreadScope::BlockedThreadScope(
        const std::shared_ptr<Task> &task, const std::shared_ptr<TaskThread> &thread
    )
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
