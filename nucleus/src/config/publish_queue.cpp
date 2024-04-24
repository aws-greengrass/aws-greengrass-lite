#include "publish_queue.hpp"
#include "scope/context_full.hpp"
#include <mutex>

namespace config {

    void PublishQueue::publish(config::PublishAction action) {
        std::scoped_lock guard{_drainMutex, _mutex};
        _actions.emplace_back(std::move(action));
        _wake.notify_one();
    }

    void PublishQueue::start() {
        std::unique_lock guard{_mutex};
        // GG-Interop: GG-Java runs thread at high priority
        // TODO: match GG-Java
        _thread = std::thread(&PublishQueue::publishThread, this);
    }

    void PublishQueue::stop() {
        _terminate = true;
        _wake.notify_all();
        // Queue will drain before joining
        if(_thread.joinable()) {
            _thread.join();
        }
    }

    void PublishQueue::publishThread() {
        scope::thread()->changeContext(context());
        for(;;) {
            std::optional<PublishAction> action = pickupAction();
            if(!action.has_value()) {
                break; // queue is empty and terminated
            }

            action.value()();

            std::unique_lock guard{_mutex};
            _actions.pop_front();
            if(_actions.empty()) {
                _drained.notify_all();
            }
        }
    }

    bool PublishQueue::drainQueue() {
        std::unique_lock drainGuard{_drainMutex, std::defer_lock};
        std::unique_lock guard{_mutex, std::defer_lock};
        std::lock(drainGuard, guard);
        _drained.wait(guard, [this]() -> bool { return _actions.empty(); });
        return true;
    }

    std::optional<PublishAction> PublishQueue::pickupAction() {
        std::unique_lock guard{_mutex};
        _wake.wait(guard, [this]() -> bool { return !_actions.empty() || _terminate.load(); });
        if(_actions.empty()) {
            return {}; // terminated and empty
        }
        PublishAction action = std::move(_actions.front());
        return action;
    }
} // namespace config
