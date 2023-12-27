#include "task_manager.hpp"
#include "expire_time.hpp"
#include "scope/context_full.hpp"
#include "task.hpp"
#include "task_threads.hpp"

namespace tasks {
    void TaskManager::queueTask(const std::shared_ptr<Task> &task) {
        if(!task->queueTaskInterlockedTrySetRunning()) {
            return; // Cannot start at this time
        }
        resumeTask(task);
    }

    void TaskManager::resumeTask(const std::shared_ptr<Task> &task) {
        // Find next thread to continue task on
        if(task->terminatesWait()) {
            return;
        }
        std::shared_ptr<TaskThread> affinity = task->getThreadAffinity();
        if(affinity) {
            // thread affinity - need to assign task to the specified thread
            // e.g. event thread of a given WASM VM or sync task
            affinity->queueTask(task);
            affinity->waken();
        } else {
            // no affinity, Add to backlog for a worker to pick up
            std::unique_lock guard{_mutex};
            if(_shutdown) {
                task->cancelTask();
                return; // abort
            }
            _backlog.push_back(task);
            guard.unlock();
            allocateNextWorker();
        }
    }

    void TaskManager::scheduleFutureTaskAssumeLocked(
        const ExpireTime &when, const std::shared_ptr<Task> &task) {
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
        const ExpireTime &when, const std::shared_ptr<Task> &task) {
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

    bool TaskManager::allocateNextWorker() {
        std::unique_lock guard{_mutex};
        if(_shutdown) {
            return false;
        }
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
            std::shared_ptr<TaskPoolWorker> worker = TaskPoolWorker::create(context());
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
        const std::shared_ptr<Task> &priorityTask) {
        std::unique_lock guard{_mutex};
        if(_shutdown || _backlog.empty()) {
            return {};
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
        if(_shutdown) {
            return {};
        }
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

    ExpireTime TaskManager::pollNextDeferredTask(TaskThread *worker) {
        std::unique_lock guard{_mutex};
        if(_shutdown) {
            return ExpireTime::unspecified();
        }
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

    TaskManager::~TaskManager() {
        // Clean shutdown of all workers and tasks that are accessible to task manager
        // This happens when the context as ref-counted to zero
        shutdownAndWait();
    }

    void TaskManager::shutdownAndWait() {
        std::unique_lock guard{_mutex};
        _shutdown = true;
        guard.unlock();
        shutdownAllWorkers(false); // non blocking
        cancelWaitingTasks(); // opportunistic
        shutdownAllWorkers(true); // blocking
        cancelWaitingTasks(); // mop-up
    }

    void TaskManager::shutdownAllWorkers(bool join) {
        std::unique_lock guard{_mutex};
        std::vector<std::shared_ptr<TaskThread>> threads;
        for(auto &worker : _busyWorkers) {
            threads.emplace_back(worker);
        }
        for(auto &worker : _idleWorkers) {
            threads.emplace_back(worker);
        }
        if(join) {
            _busyWorkers.clear();
            _idleWorkers.clear();
        }
        guard.unlock(); // avoid deadlocks
        for(auto &thread : threads) {
            thread->shutdown();
        }
        if(join) {
            for(auto &thread : threads) {
                thread->join();
            }
        }
    }

    void TaskManager::cancelWaitingTasks() {
        std::unique_lock guard{_mutex};
        std::vector<std::shared_ptr<Task>> tasks;
        for(auto &task : _backlog) {
            tasks.emplace_back(task);
        }
        for(auto &task : _delayedTasks) {
            tasks.emplace_back(task.second);
        }
        _backlog.clear();
        _delayedTasks.clear();
        guard.unlock(); // avoid deadlocks
        for(auto &task : tasks) {
            task->cancelTask();
        }
    }

} // namespace tasks
