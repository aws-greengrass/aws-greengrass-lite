#include "publish_queue.hpp"
#include "scope/context_full.hpp"

namespace config {

    void PublishQueue::publish(config::PublishAction action) {
        std::unique_lock guard{_mutex};
        _actions.emplace_back(std::move(action));
        _wake.notify_all();
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
        scope::Context::thread().changeContext(_context.lock());
        for(;;) {
            std::optional<PublishAction> action = pickupAction();
            if(action.has_value()) {
                action.value()();
            } else {
                break; // queue is empty and terminated
            }
        }
    }

    bool PublishQueue::drainQueue() {
        std::unique_lock guard{_mutex};
        while(!_actions.empty()) {
            _drained.wait(guard);
        }
        return _actions.empty();
    }

    std::optional<PublishAction> PublishQueue::pickupAction() {
        std::unique_lock guard{_mutex};
        while(_actions.empty() && !_terminate) {
            _drained.notify_all();
            _wake.wait(guard);
        }
        if(_actions.empty()) {
            return {}; // terminated and empty
        }
        PublishAction action = std::move(_actions.front());
        _actions.pop_front();
        return action;
    }
} // namespace config
