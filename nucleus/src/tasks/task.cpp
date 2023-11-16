#include "task.hpp"
#include "scope/context_full.hpp"
#include "tasks/expire_time.hpp"
#include "tasks/task_manager.hpp"
#include "tasks/task_threads.hpp"
#include <iostream>

namespace tasks {

    CurrentTaskScope::CurrentTaskScope(const std::shared_ptr<Task> &activeTask)
        : _activeTask(activeTask) {
        _oldTask = scope::Context::thread().setActiveTask(_activeTask);
    }
    CurrentTaskScope::~CurrentTaskScope() {
        scope::Context::thread().setActiveTask(_oldTask);
    }

    void Task::addSubtask(std::unique_ptr<SubTask> subTask) {
        std::unique_lock guard{_mutex};
        _subtasks.emplace_back(std::move(subTask));
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

    bool Task::queueTaskInterlockedTrySetRunning() {
        //
        // This logic including double-locking ensures the correct interplay with setStartTime()
        //
        auto taskManager = getTaskManager();
        if(!taskManager) {
            // context deleted, tasks implicitly cancelled
            cancelTask();
            return false;
        }
        std::scoped_lock multi{_mutex, taskManager->_mutex};
        if(taskManager->_shutdown) {
            cancelTask();
            return false;
        }
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
        // Task is now live, it needs to be concretely owned by task manager
        // calling getSelf will return task manager's handle
        data::ObjectAnchor anchor = taskManager->_root->anchor(ref<Task>());
        _self = anchor.getHandle();
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
        if(_lastStatus == Cancelled || _lastStatus == Completed || _lastStatus == Finalizing) {
            return; // cannot cancel in these states
        }
        auto taskManager = getTaskManager();
        if(!taskManager) {
            assert(_lastStatus == Pending);
            // Cancelled before queued
            _lastStatus = Cancelled;
            releaseBlockedThreads(true);
            return;
        }
        if(_lastStatus == Pending) {
            // Cancelling delayed task, need to unschedule
            auto start = _start;
            _lastStatus = Cancelled; // prevents task starting
            guard.unlock();
            std::scoped_lock multi{_mutex, taskManager->_mutex};
            taskManager->descheduleFutureTaskAssumeLocked(start, ref<Task>());
            releaseBlockedThreads(true);
            return;
        }
        // cancelling running task
        _lastStatus = Cancelled;
        releaseBlockedThreads(true);
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
        auto taskManager = getTaskManager();
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
        // status may have changed while re-locking (e.g. queueTask called again)
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
        std::shared_ptr<Task> taskObj{ref<Task>()};
        CurrentTaskScope scopeThatTaskIsAssociatedWithThread{taskObj};
        std::shared_ptr<data::StructModelBase> dataIn{getData()};
        std::shared_ptr<data::StructModelBase> dataOut;
        try {
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
            requeueTask(); // move to another thread if applicable
            return status;
        } catch(const std::exception &e) {
            std::cerr << "Exception during task execution: " << e.what() << std::endl;
            cancelTask();
            return Cancelled;
        } catch(...) {
            std::cerr << "Exception during task execution: (Unknown)" << std::endl;
            cancelTask();
            return Cancelled;
        }
    }

    void Task::requeueTask() {
        auto taskManager = getTaskManager();
        if(!taskManager) {
            cancelTask();
            return;
        }
        taskManager->resumeTask(ref<Task>());
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

    bool Task::waitForCompletion(const ExpireTime &terminateTime) {
        BlockedThreadScope scope{ref<Task>(), TaskThread::getThreadContext()};
        scope.taskStealing(terminateTime); // exception safe
        return isCompleted();
    }

    Task::Status Task::runInThreadCallNext(
        const std::shared_ptr<Task> &task,
        const std::shared_ptr<data::StructModelBase> &dataIn,
        std::shared_ptr<data::StructModelBase> &dataOut
    ) {
        assert(task->getSelf());
        for(;;) {
            std::unique_ptr<SubTask> subTask;
            Status status = removeSubtask(subTask);
            if(status != Running) {
                return status;
            }

            // Below scope ensures local resources - handles and thread local data - are cleaned up
            // when plugin code returns.
            scope::StackScope scopeToEnsureStackResourcesCleanedUp{};

            dataOut = subTask->runInThread(task, dataIn);
            if(dataOut != nullptr) {
                return HasReturnValue;
            }
        }
    }

    void Task::beforeRemove(const data::ObjectAnchor &anchor) {
        if(anchor.getHandle() == getSelf()) {
            // if the task handle itself is released, perform an implicit cancel
            cancelTask();
        }
    }

    void SubTask::setAffinity(const std::shared_ptr<TaskThread> &affinity) {
        _threadAffinity = affinity;
    }

    std::shared_ptr<TaskThread> SubTask::getAffinity(
        const std::shared_ptr<TaskThread> &defaultThread
    ) {
        std::shared_ptr<TaskThread> affinity = _threadAffinity;
        if(affinity) {
            return affinity;
        } else {
            return defaultThread;
        }
    }
} // namespace tasks
