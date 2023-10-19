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
        std::shared_ptr<TaskThread> affinity = task->getThreadAffinity();
        if(affinity) {
            // thread affinity - need to assign task to the specified thread
            // e.g. event thread of a given WASM VM
            affinity->queueTask(task);
            affinity->waken();
        } else {
            // no affinity, just add to backlog, a worker will pick this up
            std::unique_lock guard{_mutex};
            _backlog.push_back(task);
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
            ExpireTime adjEnd = blockedTask->getTimeout(end); // Note: timeout may change
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

    void Task::markTaskComplete() {
        std::unique_lock guard{_mutex};
        assert(_lastStatus != Cancelled);
        _lastStatus = Completed;
        // Wake up all blocked threads so they can terminate
        for(const auto &i : _blockedThreads) {
            i->waken();
        }
        _blockedThreads.clear();
    }

    void Task::cancelTask() {
        std::unique_lock guard{_mutex};
        if(_lastStatus != Completed && _lastStatus != Finalizing) {
            _lastStatus = Cancelled;
        }
        // Wake up all blocked threads so they can terminate
        for(const auto &i : _blockedThreads) {
            i->waken();
        }
        _blockedThreads.clear();
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
        std::shared_ptr<TaskThread> affinity = _subtasks.front()->getAffinity();
        if(affinity && affinity != TaskThread::getThreadContext()) {
            return SwitchThread; // task cannot run on current thread
        }
        subTask = std::move(_subtasks.front());
        _subtasks.pop_front();
        return Running;
    }

    class ThreadSelf {
    private:
        data::ObjHandle _oldHandle;

    public:
        ThreadSelf(const ThreadSelf &) = delete;
        ThreadSelf(ThreadSelf &&) = delete;
        ThreadSelf &operator=(const ThreadSelf &) = delete;
        ThreadSelf &operator=(ThreadSelf &&) = delete;

        explicit ThreadSelf(data::ObjHandle newHandle) {
            _oldHandle = Task::getSetThreadSelf(newHandle);
        }

        ~ThreadSelf() {
            Task::getSetThreadSelf(_oldHandle);
        }
    };

    std::shared_ptr<TaskThread> Task::getThreadAffinity() {
        // What thread is allowed to handle the next sub-task?
        std::unique_lock guard{_mutex};
        if(_subtasks.empty()) {
            return nullptr;
        }
        return _subtasks.front()->getAffinity();
    }

    Task::Status Task::runInThread() {
        std::shared_ptr<Task> taskObj{std::static_pointer_cast<Task>(shared_from_this())};
        ThreadSelf threadSelf(getSelf());
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
} // namespace tasks
