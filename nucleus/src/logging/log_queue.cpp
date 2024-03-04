#include "log_queue.hpp"
#include "log_manager.hpp"
#include "scope/context_full.hpp"

namespace logging {

    void LogQueue::publish(
        const std::shared_ptr<LogState> &state,
        const std::shared_ptr<data::StructModelBase> &entry) {

        std::unique_lock guard{_mutex};
        if(_terminate.load()) {
            return; // if terminating, drop everything
        }
        _entries.emplace_back(state, entry);
        if(!_running.exchange(true)) {
            _thread = std::thread(&LogQueue::publishThread, this);
        }
        _wake.notify_all();
    }

    void LogQueue::reconfigure(const std::shared_ptr<LogState> &state) {
        publish(state, {});
    }

    void LogQueue::stop() {
        std::unique_lock guard{_mutex};
        _terminate = true; // happens before _wake and _running check
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
            if(entry.has_value()) {
                processEntry(entry.value());
            } else {
                break; // queue is empty and terminated
            }
        }
    }

    bool LogQueue::drainQueue() {
        std::unique_lock guard{_mutex};
        while(!_entries.empty()) {
            _drained.wait(guard);
        }
        return _entries.empty();
    }

    std::optional<LogQueue::QueueEntry> LogQueue::pickupEntry() {
        std::unique_lock guard{_mutex};
        while(_entries.empty() && !_terminate) {
            guard.unlock();
            syncOutputs();
            _drained.notify_all();
            guard.lock();
            _wake.wait(guard);
        }
        // lock held
        if(_entries.empty()) {
            return {}; // terminated and empty
        }
        auto entry = std::move(_entries.front());
        _entries.pop_front();
        return entry;
    }

    void LogQueue::setWatch(const std::function<bool(const QueueEntry &entry)> &fn) {

        std::unique_lock guard{_mutex};
        _watch = fn;
        _watching = static_cast<bool>(fn);
    }

    void LogQueue::processEntry(const QueueEntry &entry) {
        if(_watching) {
            std::unique_lock guard{_mutex};
            const auto &fn = _watch;
            guard.unlock();
            if(fn) {
                bool resume = fn(entry);
                if(!resume) {
                    return;
                }
            }
        }
        if(entry.second) {
            // only used in this thread
            _needsSync.insert(entry.first->getContextName());
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
            for(auto &name : _needsSync) {
                auto state = logMgr.getState(name);
                if(state) {
                    state->syncOutput();
                }
            }
        }
        _needsSync.clear();
    }

    LogQueue::~LogQueue() {
        assert(!_running.load());
    }
} // namespace logging
