#include "log_queue.hpp"
#include "log_manager.hpp"
#include "scope/context_full.hpp"

namespace logging {

    void LogQueue::publish(
        std::shared_ptr<LogState> state, std::shared_ptr<data::StructModelBase> entry) {
        std::scoped_lock guard{_drainMutex, _mutex};
        if(_terminate.load()) {
            return; // if terminating, drop everything
        }
        _entries.emplace_back(std::move(state), std::move(entry));
        if(!_running.exchange(true)) {
            _thread = std::thread(&LogQueue::publishThread, this);
        }
        _wake.notify_one();
    }

    void LogQueue::reconfigure(const std::shared_ptr<LogState> &state) {
        publish(state, {});
    }

    void LogQueue::stop() {
        std::unique_lock guard{_mutex};
        _terminate.store(true); // happens before _wake and _running check
        _wake.notify_all();
        if(_running.exchange(false)) {
            guard.unlock();
            _thread.join();
        }
    }

    void LogQueue::publishThread() {
        scope::thread()->changeContext(context());
        for(;;) {
            auto entry = pickupEntry();
            if(!entry.has_value()) {
                break; // queue is empty and terminated
            }

            processEntry(entry.value());

            std::unique_lock guard{_mutex};
            // assumes single-reader (i.e. this publish thread)
            _entries.pop_front();
            if(_entries.empty()) {
                _drained.notify_all();
            }
        }
    }

    bool LogQueue::drainQueue() {
        std::unique_lock drainGuard{_drainMutex, std::defer_lock};
        std::unique_lock guard{_mutex, std::defer_lock};
        std::lock(drainGuard, guard);
        _drained.wait(guard, [this]() -> bool { return _entries.empty(); });
        return true;
    }

    std::optional<LogQueue::QueueEntry> LogQueue::pickupEntry() {
        std::unique_lock guard{_mutex};
        if(!_needsSync.empty() && _entries.empty() && !_terminate.load()) {
            guard.unlock();
            syncOutputs();
            guard.lock();
        }
        _wake.wait(guard, [this]() -> bool { return !_entries.empty() || _terminate.load(); });
        if(_entries.empty()) {
            return {}; // terminated and empty
        }

        auto entry = std::move(_entries.front());
        return entry;
    }

    void LogQueue::setWatch(std::function<bool(const QueueEntry &entry)> fn) {
        std::unique_lock guard{_mutex};
        _watching.store(static_cast<bool>(fn));
        _watch = std::move(fn);
    }

    void LogQueue::processEntry(const QueueEntry &entry) {
        if(_watching) {
            const auto fn = [this]() {
                std::unique_lock guard{_mutex};
                return _watch;
            }();
            if(fn) {
                bool resume = fn(entry);
                if(!resume) {
                    return;
                }
            }
        }
        if(entry.second) {
            // only used in this thread
            _needsSync.emplace(entry.first->getContextName());
            entry.first->writeLog(entry.second);
        } else {
            entry.first->changeOutput();
        }
    }

    void LogQueue::syncOutputs() {
        // Single threaded, lock free, needsSync only used in this thread
        auto ctx = context();
        if(ctx) {
            auto &logMgr = ctx->logManager();
            for(const auto &name : _needsSync) {
                auto state = logMgr.getState(name);
                if(state) {
                    state->syncOutput();
                }
            }
        }
        _needsSync.clear();
    }

    LogQueue::~LogQueue() noexcept {
        assert(!_running.load());
    }
} // namespace logging
