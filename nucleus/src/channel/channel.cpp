#include "channel.hpp"
#include "cpp_api.hpp"
#include "data/shared_buffer.hpp"
#include "tasks/task_callbacks.hpp"
#include <iostream>
#include <mutex>

namespace channel {
    Channel::~Channel() noexcept {
        _terminate = true;
        _wait.notify_one();
        if(_worker.joinable()) {
            _worker.join();
        }
    }

    void Channel::channelWorker() {
        for(;;) {
            std::unique_lock _lock(_mutex);
            _wait.wait(_lock, [this]() { return !_inFlight.empty() || _terminate || _closed; });
            if(_terminate) {
                return;
            }
            while(!_inFlight.empty()) {
                auto data = std::move(_inFlight.front());
                _inFlight.pop();
                _lock.unlock();
                _listener->invokeChannelListenCallback(data);
                _lock.lock();
            }
            if(_closed) {
                for(const auto &c : _onClose) {
                    c->invokeChannelCloseCallback();
                }
                return;
            }
        };
    }

    void Channel::write(const std::shared_ptr<data::StructModelBase> &value) {
        std::unique_lock _lock(_mutex);
        if(!_closed) {
            _inFlight.push(value);
            _wait.notify_one();
        }
    }

    void Channel::close() {
        _closed = true;
        _wait.notify_one();
    }

    void Channel::setListenCallback(const std::shared_ptr<tasks::Callback> &callback) {
        std::unique_lock _lock(_mutex);
        _listener = callback;
        if(!_workerStarted.exchange(true)) {
            _worker = std::thread(&Channel::channelWorker, this);
        }
    }

    void Channel::setCloseCallback(const std::shared_ptr<tasks::Callback> &callback) {
        std::unique_lock _lock(_mutex);
        _onClose.push_back(callback);
    }
} // namespace channel
