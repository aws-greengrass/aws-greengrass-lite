#pragma once
#include "data/struct_model.hpp"
#include "data/tracked_object.hpp"
#include <atomic>
#include <mutex>
#include <queue>

namespace tasks {
    class Callback;
    class Task;
    class ExpireTime;
} // namespace tasks

namespace channel {

    /**
     * Core Base functionality that is not template aware
     */
    class ChannelBaseCore : public data::TrackedObject {
    protected:
        mutable std::mutex _mutex;
        std::shared_ptr<tasks::Task> _pendingTask;
        std::atomic_bool _closed{false};

        virtual bool doReceiveWork(std::unique_lock<std::mutex> &lock) = 0;
        virtual bool doCloseWork(std::unique_lock<std::mutex> &lock) = 0;
        virtual bool isEmpty(std::unique_lock<std::mutex> &lock) const = 0;
        virtual bool canProcessWork(std::unique_lock<std::mutex> &lock) const = 0;
        virtual void doIdle() const {
        }

        bool doIdle(std::unique_lock<std::mutex> &lock) const;
        void channelWork(
            const std::shared_ptr<channel::ChannelBaseCore> &keepInMemory,
            const std::shared_ptr<data::StructModelBase> &dummy);

        void queueWork(std::unique_lock<std::mutex> &lock);

    public:
        explicit ChannelBaseCore(const scope::UsingContext &context)
            : data::TrackedObject{context} {
        }
        bool drain();
    };

    /**
     * Base implementation of channel as a template allowing tight control of types and callbacks.
     */
    template<typename DataType>
    class ChannelBase : public ChannelBaseCore {
    protected:
        std::queue<DataType> _inFlight;

        virtual void doReceive(const DataType &) = 0;
        virtual void doClose() const {
        }

        void doListenerWork(std::unique_lock<std::mutex> &lock) override {
            auto listener = _listener;
            while(!_inFlight.empty() && listener.has_value()) {
                auto data = std::move(_inFlight.front());
                _inFlight.pop();
                lock.unlock();
                Traits::invokeListenCallback(listener.value(), data);
                lock.lock();
                if(_inFlight.empty()) {
                    // While it seems this should live outside of while loop,
                    // when lock is released and reclaimed, we may no longer be idle
                    doIdle(lock);
                }
            }
        }

        void doCloseWork(std::unique_lock<std::mutex> &lock) override {
            if(!_inFlight.empty()) {
                return; // cannot "close" until listener registered
            }
            while(_closed && !_onClose.empty()) {
                auto allOnClose = _onClose;
                _onClose.clear();
                for(const auto &c : allOnClose) {
                    lock.unlock();
                    Traits::invokeCloseCallback(c);
                    lock.lock();
                }
            }
        }

        void queueTaskOnDemand(std::unique_lock<std::mutex> &lock) override {
            assert(lock.owns_lock());
            if(!_pendingTask) {
                return; // Task already queued or in progress
            }
            if((_listener && !_inFlight.empty()) || (_closed && !_onClose.empty())) {
                queueWork(lock);
            }
        }

    public:
        explicit ChannelBase(const scope::UsingContext &context) : ChannelBaseCore{context} {
        }
        ChannelBase(const ChannelBase &) = delete;
        ChannelBase(ChannelBase &&) = delete;
        ChannelBase &operator=(const ChannelBase &) = delete;
        ChannelBase &operator=(ChannelBase &&) = delete;
        ~ChannelBase() noexcept override = default;

        void write(const DataType &value) {
            std::unique_lock lock(_mutex);
            if(!_closed) {
                _inFlight.emplace(value);
                queueTaskOnDemand(lock);
            }
        }

        void close() {
            std::unique_lock lock(_mutex);
            _closed = true;
            queueTaskOnDemand(lock);
        }

        void setListenCallback(const ListenCallbackType &callback) {
            std::unique_lock lock(_mutex);
            _listener = callback;
            queueTaskOnDemand(lock);
        }

        void setCloseCallback(const CloseCallbackType &callback) {
            std::unique_lock lock(_mutex);
            _onClose.push_back(callback);
            queueTaskOnDemand(lock);
        }

        void setIdleCallback(const IdleCallbackType &callback) {
            std::unique_lock lock(_mutex);
            _onIdle.push_back(callback);
        }

        bool isListening() {
            std::unique_lock lock(_mutex);
            return _listener.has_value();
        }

        bool empty() const override {
            std::unique_lock lock(_mutex);
            return _inFlight.empty();
        }
    };

} // namespace channel
