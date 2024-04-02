#include "local_topics.hpp"
#include "scope/context_full.hpp"
#include "tasks/task.hpp"
#include "tasks/task_callbacks.hpp"
#include <shared_mutex>

namespace pubsub {

    void Listener::closeImpl() noexcept {
        if(!_parent.expired()) {
            std::shared_ptr<Listeners> listeners{_parent};
            if(listeners) {
                listeners->cleanup();
            }
        }
    }

    void Listener::close() {
        closeImpl();
    }

    Listener::~Listener() noexcept {
        closeImpl();
    }

    Listener::Listener(
        const scope::UsingContext &context,
        data::Symbol topicOrd,
        const std::shared_ptr<Listeners> &listeners,
        const std::shared_ptr<tasks::Callback> &callback)
        : data::TrackedObject(context), _topic(topicOrd), _parent(listeners), _callback(callback) {
    }

    Listeners::Listeners(const scope::UsingContext &context, data::Symbol topic)
        : scope::UsesContext(context), _topic(topic) {
    }

    void Listeners::cleanup() {
        // scoped lock
        {
            std::unique_lock guard{managerMutex()};
            std::ignore =
                std::remove_if(_listeners.begin(), _listeners.end(), [&](const auto &item) {
                    return item.expired();
                });
        }
        // lock must be released before this step
        auto ctx = context();
        if(_listeners.empty() && ctx) {
            manager().cleanup();
        }
    }

    void PubSubManager::cleanup() {
        std::unique_lock guard{managerMutex()};
        for(auto i = _topics.begin(); i != _topics.end(); ++i) {
            if(i->second->isEmptyMutexHeld()) {
                _topics.erase(i);
            }
        }
    }

    std::shared_ptr<Listener> Listeners::addNewListener(
        const std::shared_ptr<tasks::Callback> &callback) {

        std::shared_ptr<Listener> listener{
            std::make_shared<Listener>(context(), _topic, baseRef(), callback)};
        std::unique_lock guard{managerMutex()};
        _listeners.push_back(listener);
        return listener;
    }

    std::shared_ptr<Listeners> PubSubManager::tryGetListeners(data::Symbol topicName) {
        std::shared_lock guard{managerMutex()};
        auto i = _topics.find(topicName);
        if(i == _topics.end()) {
            return {};
        } else {
            return i->second;
        }
    }

    std::shared_ptr<Listeners> PubSubManager::getListeners(data::Symbol topicName) {
        std::shared_ptr<Listeners> listeners = tryGetListeners(topicName);
        if(listeners) {
            return listeners;
        }
        std::unique_lock guard{managerMutex()};
        auto i = _topics.find(topicName);
        if(i != _topics.end()) {
            // rare edge case
            return i->second;
        }
        listeners = std::make_shared<Listeners>(context(), topicName);
        _topics.emplace(topicName, listeners);
        return listeners;
    }

    std::shared_ptr<Listener> PubSubManager::subscribe(
        data::Symbol topic, const std::shared_ptr<tasks::Callback> &callback) {
        std::shared_ptr<Listeners> listeners = getListeners(topic);
        std::shared_ptr<Listener> listener = listeners->addNewListener(callback);
        return listener;
    }

    std::shared_ptr<FutureBase> PubSubManager::callFirst(
        data::Symbol topic, const std::shared_ptr<data::ContainerModelBase> &dataIn) {
        if(!dataIn) {
            throw std::runtime_error("Data must be passed into an LPC call");
        }
        if(!topic) {
            throw std::runtime_error("Topic must be passed into an LPC call");
        }
        auto listeners = getListeners(topic);
        std::vector<std::shared_ptr<Listener>> callOrder;
        listeners->fillTopicListeners(callOrder);
        for(const auto &i : callOrder) {
            auto future = i->call(dataIn);
            if(future) {
                return future;
            }
        }
        return {};
    }

    std::vector<std::shared_ptr<FutureBase>> PubSubManager::callAll(
        data::Symbol topic, const std::shared_ptr<data::ContainerModelBase> &dataIn) {
        if(!dataIn) {
            throw std::runtime_error("Data must be passed into an LPC call");
        }
        if(!topic) {
            throw std::runtime_error("Topic must be passed into an LPC call");
        }
        auto listeners = getListeners(topic);
        std::vector<std::shared_ptr<Listener>> callOrder;
        std::vector<std::shared_ptr<FutureBase>> futures;
        listeners->fillTopicListeners(callOrder);
        for(const auto &i : callOrder) {
            auto future = i->call(dataIn);
            if(future) {
                futures.emplace_back(future);
            }
        }
        return futures;
    }

    void Listeners::fillTopicListeners(std::vector<std::shared_ptr<Listener>> &callOrder) {
        std::shared_lock guard{managerMutex()};
        for(auto ri = _listeners.rbegin(); ri != _listeners.rend(); ++ri) {
            if(!ri->expired()) {
                callOrder.push_back(ri->lock());
            }
        }
    }

    std::shared_mutex &Listeners::managerMutex() {
        return manager().managerMutex();
    }
    PubSubManager &Listeners::manager() const {
        return context()->lpcTopics();
    }

    std::shared_ptr<pubsub::FutureBase> Listener::call(
        const std::shared_ptr<data::ContainerModelBase> &dataIn) {
        return _callback->invokeTopicCallback(_topic, dataIn);
    }
} // namespace pubsub
