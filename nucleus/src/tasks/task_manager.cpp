#include "task_manager.hpp"
#include "expire_time.hpp"
#include "scope/context_full.hpp"
#include "task.hpp"
#include "task_threads.hpp"

namespace tasks {

    void TaskManager::queueTask(const std::shared_ptr<Task> &task) {
        // no affinity, Add to backlog for a worker to pick up
        std::unique_lock guard{_mutex};
        if(_shutdown) {
            return; // abort
        }
        _backlog.push_back(task);
        guard.unlock();
        allocateNextWorker();
    }

    void TaskManager::queueTask(const std::shared_ptr<Task> &task, const ExpireTime &when) {
        // no affinity, Add to backlog for a worker to pick up
        std::unique_lock guard{_mutex};

        if(_shutdown) {
            return; // abort
        }

        if(!_timerWorker) {
            _timerWorker = TimerWorker::create(context());
        }

        auto first = _delayedTasks.begin();
        bool needsSignal = first == _delayedTasks.end() || when < first->first;
        _delayedTasks.emplace(when, task); // insert into sorted order (duplicates ok)
        if(needsSignal) {
            // Wake timer thread early - its wait time is incorrect
            _timerWorker->waken();
        }
    }

    bool TaskManager::allocateNextWorker() {
        std::unique_lock guard{_mutex};
        if(_shutdown) {
            return false;
        }
        if(_backlog.empty()) {
            // no work to do, no need to allocate a worker
            return true;
        }
        if(_idleWorkers.empty()) {
            // Needing to create workers, don't consider clean-up yet
            _confirmedIdleWorkers = 0;
            _nextDecayCheck = ExpireTime::fromNowMillis(_decayMs);
            if(_maxWorkers > 0 && _busyWorkers.size() >= static_cast<uint64_t>(_maxWorkers)) {
                return false; // run out of workers
            }
            // allocate a new worker
            auto worker = TaskPoolWorker::create(context());
            auto pWorker = worker.get();
            _busyWorkers.emplace_back(std::move(worker));
            pWorker->waken();
            return true;
        } else {
            auto worker = std::move(_idleWorkers.back());
            _idleWorkers.pop_back();
            if(_idleWorkers.size() < _confirmedIdleWorkers) {
                // demand on idle pool, defer cleanup
                _confirmedIdleWorkers = util::safeBoundPositive<int64_t>(_idleWorkers.size());
                _nextDecayCheck = ExpireTime::fromNowMillis(_decayMs);
            }
            auto pWorker = worker.get();
            _busyWorkers.emplace_back(std::move(worker));
            pWorker->waken();
            return true;
        }
    }

    std::shared_ptr<Task> TaskManager::acquireTaskForWorker(TaskPoolWorker *worker) {
        std::unique_lock guard{_mutex};
        if(_shutdown) {
            return {};
        }
        if(_backlog.empty()) {
            // backlog is empty, need to idle this worker
            for(auto i = _busyWorkers.begin(); i != _busyWorkers.end(); ++i) {
                if(i->get() == worker) {
                    _idleWorkers.emplace_back(std::move(*i));
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

    /**
     * Queue all tasks who's start time has passed
     * @return start time of next task
     */
    ExpireTime TaskManager::computeNextDeferredTask() {
        std::unique_lock guard{_mutex};
        if(_shutdown) {
            return ExpireTime::unspecified();
        }
        ExpireTime nextTime = _nextDecayCheck;

        for(auto it = _delayedTasks.begin(); it != _delayedTasks.end();
            it = _delayedTasks.begin()) {

            nextTime = it->first; // always from top, see note below
            if(nextTime <= ExpireTime::now()) {
                // time has expired
                auto task = it->second;
                _delayedTasks.erase(it);
                guard.unlock();
                // Note, at this point, _delayedTasks may get modified, and the iterator
                // must be considered invalid. Given that we always remove head in each iteration
                // we don't need to do any tricks to maintain the iterator.
                queueTask(task);
                guard.lock();
            } else {
                return nextTime;
            }
        }
        return ExpireTime::infinite();
    }

    /**
     * If decay time has passed, release threads
     * @return next decay timer time
     */
    ExpireTime TaskManager::computeIdleTaskDecay() {
        std::unique_lock guard{_mutex};
        if(_shutdown) {
            return ExpireTime::unspecified();
        }
        auto now = ExpireTime::now();
        if(now >= _nextDecayCheck) {
            auto decay = _confirmedIdleWorkers;
            decay =
                std::max(decay, util::safeBoundPositive<uint64_t>(_idleWorkers.size())); // safety
            auto minIdle = _minIdle;
            // 'decay' number of workers have not been used for a while, we can shrink thread
            // pool
            for(; decay > minIdle; --decay) {
                auto worker = std::move(_idleWorkers.back());
                _idleWorkers.pop_back();
                worker->join();
            }
            _confirmedIdleWorkers = util::safeBoundPositive<int64_t>(_idleWorkers.size());
            _nextDecayCheck = ExpireTime::fromNowMillis(_decayMs);
        }
        return _nextDecayCheck;
    }

    TaskManager::~TaskManager() {
        // Clean shutdown of all workers and tasks that are accessible to task manager
        // This happens when the context as ref-counted to zero
        shutdownAndWait();
    }

    void TaskManager::shutdownAndWait() {
        std::unique_lock guard{_mutex};
        _shutdown = true; // prevent new tasks being added
        std::vector<std::shared_ptr<TaskPoolWorker>> threads;
        for(auto &worker : _busyWorkers) {
            threads.emplace_back(std::move(worker));
        }
        for(auto &worker : _idleWorkers) {
            threads.emplace_back(std::move(worker));
        }
        if(_timerWorker) {
            threads.emplace_back(std::move(_timerWorker));
        }
        _busyWorkers.clear();
        _idleWorkers.clear();
        _backlog.clear();
        _delayedTasks.clear();
        guard.unlock(); // avoid deadlocks
        for(auto &thread : threads) {
            thread->shutdown();
        }
        for(auto &thread : threads) {
            thread->join();
        }
    }

} // namespace tasks
