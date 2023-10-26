#include "task.hpp"
#include "expire_time.hpp"

namespace tasks {

    data::ObjectAnchor TaskManager::createTask() {
        auto task{std::make_shared<Task>(_environment)};
        auto taskAnchor{anchor(task)};
        task->setSelf(taskAnchor.getHandle());
        return taskAnchor;
    }

    void TaskManager::queueTask(const std::shared_ptr<Task> &task) {
        if(!task->queueTaskInterlockedTrySetRunning(ref<TaskManager>())) {
            return; // Cannot start at this time
        }
        // Following code executes only one time per task
        std::shared_ptr<TaskThread> affinity = task->getThreadAffinity();
        if(affinity) {
            // thread affinity - need to assign task to the specified thread
            // e.g. event thread of a given WASM VM or sync task
            affinity->queueTask(task);
            affinity->waken();
        } else {
            // no affinity, Add to backlog for a worker to pick up
            std::unique_lock guard{_mutex};
            _backlog.push_back(task);
            guard.unlock();
            allocateNextWorker();
        }
    }

    void TaskManager::scheduleFutureTaskAssumeLocked(
        const ExpireTime &when, const std::shared_ptr<Task> &task
    ) {
        auto first = _delayedTasks.begin();
        bool needsSignal = first == _delayedTasks.end() || when < first->first;
        _delayedTasks.emplace(when, task); // insert into sorted order (duplicates ok)
        auto thread{_timerWorkerThread.lock()};
        if(thread && needsSignal) {
            // Wake timer thread early - its wait time is incorrect
            thread->waken();
        }
    }

    void TaskManager::descheduleFutureTaskAssumeLocked(
        const ExpireTime &when, const std::shared_ptr<Task> &task
    ) {
        if(when == ExpireTime::unspecified()) {
            return;
        }
        for(auto it = _delayedTasks.find(when); it != _delayedTasks.end(); ++it) {
            if(it->second == task) {
                _delayedTasks.erase(it);
                return;
            }
        }
    }

    void TaskThread::queueTask(const std::shared_ptr<Task> &task) {
        std::unique_lock guard{_mutex};
        _tasks.push_back(task);
    }

    bool TaskManager::allocateNextWorker() {
        std::unique_lock guard{_mutex};
        if(_backlog.empty()) {
            return true; // backlog is empty (does not include thread-assigned
                         // tasks)
        }
        if(_idleWorkers.empty()) {
            if(_busyWorkers.size() >= _maxWorkers) {
                return false; // run out of workers
            }
            // allocate a new worker - it will be tasked with picking up next task
            // TODO: add some kind of knowledge of workers starting
            // code as is can cause a scramble
            std::shared_ptr<TaskManager> pool{
                std::static_pointer_cast<TaskManager>(this->shared_from_this())};
            std::shared_ptr<TaskPoolWorker> worker{
                std::make_shared<TaskPoolWorker>(_environment, pool)};
            _busyWorkers.push_back(worker);
            worker->waken();
            return true;
        } else {
            std::shared_ptr<TaskPoolWorker> worker{_idleWorkers.back()};
            _idleWorkers.pop_back();
            _busyWorkers.push_back(worker);
            worker->waken();
            return true;
        }
    }

    std::shared_ptr<Task> TaskManager::acquireTaskWhenStealing(
        TaskThread *worker, const std::shared_ptr<Task> &priorityTask
    ) {
        std::unique_lock guard{_mutex};
        if(_backlog.empty()) {
            return nullptr;
        }
        for(auto i = _backlog.begin(); i != _backlog.end(); ++i) {
            if(*i == priorityTask) {
                _backlog.erase(i);
                return priorityTask; // claim priority task
            }
        }
        // permitted to take any backlog task if blocked
        std::shared_ptr<Task> work{_backlog.front()};
        _backlog.pop_front();
        return work;
    }

    std::shared_ptr<Task> TaskManager::acquireTaskForWorker(TaskThread *worker) {
        std::unique_lock guard{_mutex};
        if(_backlog.empty()) {
            // backlog is empty, need to idle this worker
            for(auto i = _busyWorkers.begin(); i != _busyWorkers.end(); ++i) {
                if(i->get() == worker) {
                    _idleWorkers.emplace_back(*i);
                    _busyWorkers.erase(i);
                    break;
                }
            }
            return nullptr; // backlog is empty
        }
        std::shared_ptr<Task> work{_backlog.front()};
        _backlog.pop_front();
        return work;
    }

    void Task::addSubtask(std::unique_ptr<SubTask> subTask) {
        std::unique_lock guard{_mutex};
        _subtasks.emplace_back(std::move(subTask));
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
        _thread.detach(); // Make this a daemon thread that never joins
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

    void TaskThread::taskStealing(const std::shared_ptr<Task> &blockedTask, const ExpireTime &end) {
        while(!blockedTask->terminatesWait()) {
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
            return nullptr;
        }
        std::shared_ptr<Task> next = _tasks.front();
        _tasks.pop_front();
        return next;
    }

    std::shared_ptr<Task> TaskThread::pickupPoolTask(const std::shared_ptr<Task> &blockingTask) {
        if(_pool.expired()) {
            return nullptr;
        }
        // TODO: possible race / handle with exception handling
        std::shared_ptr<TaskManager> pool = _pool.lock();
        return pool->acquireTaskWhenStealing(this, blockingTask);
    }

    std::shared_ptr<Task> TaskThread::pickupPoolTask() {
        if(_pool.expired()) {
            return nullptr;
        }
        // TODO: possible race / handle with exception handling
        std::shared_ptr<TaskManager> pool = _pool.lock();
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

    void Task::signalBlockedThreads(bool isLocked) {
        std::unique_lock guard{_mutex, std::defer_lock};
        if(!isLocked) {
            guard.lock();
        }
        for(const auto &i : _blockedThreads) {
            i->waken();
        }
    }

    void Task::releaseBlockedThreads(bool isLocked) {
        std::unique_lock guard{_mutex, std::defer_lock};
        if(!isLocked) {
            guard.lock();
        }
        signalBlockedThreads(true);
        _blockedThreads.clear();
    }

    bool Task::queueTaskInterlockedTrySetRunning(const std::shared_ptr<TaskManager> &taskManager) {
        //
        // This logic including double-locking ensures the correct interplay with setStartTime()
        //
        std::scoped_lock multi{_mutex, taskManager->_mutex};
        _taskManager = taskManager;
        if(_lastStatus != Pending) {
            return false; // already started, cannot start again
        }
        // Clean up from schedule queue - prevents double-insert edge cases
        // It's possible we'll remove and re-insert, but that makes for simpler logic
        taskManager->descheduleFutureTaskAssumeLocked(_start, ref<Task>());
        if(_start > ExpireTime::now()) {
            // start in future
            taskManager->scheduleFutureTaskAssumeLocked(_start, ref<Task>());
            return false;
        }
        _lastStatus = Running;
        return true;
    }

    void Task::markTaskComplete() {
        std::unique_lock guard{_mutex};
        assert(_lastStatus != Cancelled);
        _lastStatus = Completed;
        releaseBlockedThreads(true);
    }

    void Task::cancelTask() {
        std::unique_lock guard{_mutex};
        std::shared_ptr<TaskManager> taskManager = getTaskManager();
        if(!taskManager) {
            assert(_lastStatus == Pending);
            // Cancelled before queued
            _lastStatus = Cancelled;
            releaseBlockedThreads(true);
            return;
        }
        if(_lastStatus == Pending) {
            // Cancelling delayed task, need to deschedule
            auto start = _start;
            _lastStatus = Cancelled; // prevents task starting
            guard.unlock();
            std::scoped_lock multi{_mutex, taskManager->_mutex};
            taskManager->descheduleFutureTaskAssumeLocked(start, ref<Task>());
            releaseBlockedThreads(true);
            return;
        }
        if(_lastStatus != Completed && _lastStatus != Finalizing) {
            // cancelling running task
            _lastStatus = Cancelled;
            releaseBlockedThreads(true);
            return;
        }
    }

    void Task::setTimeout(const ExpireTime &terminateTime) {
        std::unique_lock guard{_mutex};
        bool needSignal = _lastStatus != Pending && terminateTime < _timeout;
        _timeout = terminateTime;
        if(needSignal) {
            // timeout change may cause task to be auto-cancelled
            signalBlockedThreads(true);
        }
    }

    bool Task::setStartTime(const ExpireTime &startTime) {
        // Lock MUST be acquired prior to obtaining taskManager reference to
        // prevent race condition with TaskManager's queueTask() function
        std::unique_lock guard{_mutex};
        std::shared_ptr<TaskManager> taskManager = getTaskManager();
        if(!taskManager) {
            assert(_lastStatus == Pending);
            // The queueTask function has not been called yet. Just set new start time and done
            _start = startTime;
            return true;
        }
        guard.unlock(); // cannot keep this held while acquiring task manager lock

        // At least one previous call to queueTask() occurred and taskManager is known
        // we now need to repeat the process taking into considering this is after the
        // queueTask() call

        std::scoped_lock multi{_mutex, taskManager->_mutex};
        // status may have changed while relocking (e.g. queueTask called again)
        if(_lastStatus != Pending) {
            return false; // already started, did not defer
        }
        taskManager->descheduleFutureTaskAssumeLocked(_start, ref<Task>());
        if(startTime < ExpireTime::now()) {
            // At this point, task can only be started via timer, so place at head of queue
            // however, cannot use unspecified(), so use the earliest possible valid time.
            _start = ExpireTime::epoch();
        } else {
            // New time is in future
            _start = startTime;
        }
        taskManager->scheduleFutureTaskAssumeLocked(_start, ref<Task>());
        return true; // rescheduled task
    }

    ExpireTime Task::getTimeout() const {
        std::unique_lock guard{_mutex};
        return _timeout;
    }

    ExpireTime Task::getStartTime() const {
        std::unique_lock guard{_mutex};
        return _start;
    }

    bool Task::terminatesWait() {
        std::shared_lock guard{_mutex};
        if(_lastStatus == Completed || _lastStatus == Cancelled) {
            // Previously completed or cancelled
            return true;
        }
        if(_lastStatus == Finalizing) {
            // protected from timeout
            return false;
        }
        if(_timeout < ExpireTime::now()) {
            // Auto-cancel on timeout
            _lastStatus = Cancelled;
            return true;
        }
        return false;
    }

    bool Task::isCompleted() {
        std::shared_lock guard{_mutex};
        return _lastStatus == Completed;
    }

    Task::Status Task::removeSubtask(std::unique_ptr<SubTask> &subTask) {
        std::unique_lock guard{_mutex};
        if(_subtasks.empty()) {
            return NoSubTasks;
        }
        std::shared_ptr<TaskThread> affinity = _subtasks.front()->getAffinity(_defaultThread);
        if(affinity && affinity != TaskThread::getThreadContext()) {
            return SwitchThread; // task cannot run on current thread
        }
        subTask = std::move(_subtasks.front());
        _subtasks.pop_front();
        return Running;
    }

    // Ensures thread-self is assigned and cleaned up with stack unwinding (RII)
    class ThreadSelf {
    private:
        data::ObjHandle _oldHandle;

    public:
        ThreadSelf(const ThreadSelf &) = delete;
        ThreadSelf(ThreadSelf &&) = delete;
        ThreadSelf &operator=(const ThreadSelf &) = delete;
        ThreadSelf &operator=(ThreadSelf &&) = delete;

        explicit ThreadSelf(Task *contextTask) {
            _oldHandle = Task::getSetThreadSelf(contextTask->getSelf());
        }

        ~ThreadSelf() {
            Task::getSetThreadSelf(_oldHandle);
        }
    };

    // Sets task thread affinity, used if subtask has no affinity
    void Task::setDefaultThread(const std::shared_ptr<TaskThread> &thread) {
        _defaultThread = thread;
    }

    std::shared_ptr<TaskThread> Task::getThreadAffinity() {
        // What thread is allowed to handle the next sub-task?
        std::unique_lock guard{_mutex};
        if(_subtasks.empty()) {
            return _defaultThread;
        }
        return _subtasks.front()->getAffinity(_defaultThread);
    }

    Task::Status Task::runInThread() {
        std::shared_ptr<Task> taskObj{std::static_pointer_cast<Task>(shared_from_this())};
        ThreadSelf threadSelf(this);
        std::shared_ptr<data::StructModelBase> dataIn{getData()};
        std::shared_ptr<data::StructModelBase> dataOut;
        Status status = runInThreadCallNext(taskObj, dataIn, dataOut);
        while(status == NoSubTasks || status == HasReturnValue) {
            status = finalizeTask(dataOut);
            if(status == Completed) {
                markTaskComplete();
                return status; // need to switch thread to continue
            } else if(status == Finalizing) {
                status = runInThreadCallNext(taskObj, dataOut, dataOut);
            } else {
                break;
            }
        }
        return status;
    }

    Task::Status Task::finalizeTask(const std::shared_ptr<data::StructModelBase> &data) {
        std::unique_lock guard{_mutex};
        if(_lastStatus == Finalizing) {
            if(_subtasks.empty()) {
                return Completed; // completed finalization step
            } else {
                return SwitchThread; // assume we may have to switch thread to
                                     // finalize
            }
        }
        _subtasks.clear(); // if data provided, abort early
        _data = data; // finalization data
        if(_finalize) {
            // move finalization function to end of call chain
            _subtasks.emplace_back(std::move(_finalize));
        }
        return _lastStatus = Finalizing; // perform finalization step in same thread
                                         // if possible
    }

    void Task::addBlockedThread(const std::shared_ptr<TaskThread> &blockedThread) {
        std::unique_lock guard{_mutex};
        _blockedThreads.emplace_back(blockedThread); // threads blocked and in
                                                     // process of task stealing
    }

    void Task::removeBlockedThread(const std::shared_ptr<TaskThread> &blockedThread) {
        std::unique_lock guard{_mutex};
        _blockedThreads.remove(blockedThread);
    }

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
        )
            : _task(task), _thread(thread) {
            _task->addBlockedThread(_thread);
        }

        void taskStealing(const ExpireTime &end) {
            _thread->taskStealing(_task, end);
        }

        ~BlockedThreadScope() {
            _task->removeBlockedThread(_thread);
        }
    };

    bool Task::waitForCompletion(const ExpireTime &expireTime) {
        BlockedThreadScope scope{ref<Task>(), TaskThread::getThreadContext()};
        scope.taskStealing(expireTime); // exception safe
        return isCompleted();
    }

    Task::Status Task::runInThreadCallNext(
        const std::shared_ptr<Task> &task,
        const std::shared_ptr<data::StructModelBase> &dataIn,
        std::shared_ptr<data::StructModelBase> &dataOut
    ) {
        for(;;) {
            std::unique_ptr<SubTask> subTask;
            Status status = removeSubtask(subTask);
            if(status != Running) {
                return status;
            }
            dataOut = subTask->runInThread(task, dataIn);
            if(dataOut != nullptr) {
                return HasReturnValue;
            }
        }
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

    ExpireTime TaskManager::pollNextDeferredTask(TaskThread *worker) {
        std::unique_lock guard{_mutex};
        if(worker) {
            _timerWorkerThread = worker->ref<TaskThread>();
        }
        for(auto it = _delayedTasks.begin(); it != _delayedTasks.end();
            it = _delayedTasks.begin()) {
            ExpireTime nextTime = it->first;
            if(nextTime <= ExpireTime::now()) {
                auto task = it->second;
                _delayedTasks.erase(it);
                guard.unlock();
                // Note, at this point, _delayedTasks may get modified, and the iterator
                // must be considered invalid. Given that we always remove head in each iteration
                // we don't need to do any tricks to maintain the iterator.
                queueTask(task);
                guard.lock();
            } else {
                // Next task to be executed in the future
                return nextTime;
            }
        }
        // No deferred tasks
        return ExpireTime::infinite();
    }
} // namespace tasks
