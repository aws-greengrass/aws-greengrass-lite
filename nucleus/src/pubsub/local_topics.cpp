#include "local_topics.hpp"
#include "scope/context_full.hpp"
#include "tasks/task.hpp"
#include "tasks/task_callbacks.hpp"
#include <shared_mutex>

namespace pubsub {
    Listener::~Listener() {
        if(!_parent.expired()) {
            std::shared_ptr<Listeners> listeners{_parent};
            listeners->cleanup();
        }
    }

    Listener::Listener(
        const std::shared_ptr<scope::Context> &context,
        data::Symbol topicOrd,
        Listeners *listeners,
        const std::shared_ptr<tasks::Callback> &callback)
        : data::TrackedObject(context), _topic(topicOrd), _parent(listeners->weak_from_this()),
          _callback(callback) {
    }

    Listeners::Listeners(const std::shared_ptr<scope::Context> &context, data::Symbol topic)
        : _context(context), _topic(topic) {
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
        if(_listeners.empty() && !_context.expired()) {
            manager().cleanup();
        }
    }

    void PubSubManager::cleanup() {
        std::unique_lock guard{managerMutex()};
        for(auto i = _topics.begin(); i != _topics.end(); ++i) {
            if(i->second->isEmpty()) {
                _topics.erase(i);
            }
        }
    }

    std::shared_ptr<Listener> Listeners::addNewListener(
        const std::shared_ptr<tasks::Callback> &callback) {
        std::shared_ptr<Listener> listener{
            std::make_shared<Listener>(_context.lock(), _topic, this, callback)};
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
        listeners = std::make_shared<Listeners>(_context.lock(), topicName);
        _topics.emplace(topicName, listeners);
        return listeners;
    }

    std::shared_ptr<Listener> PubSubManager::subscribe(
        data::Symbol topic, const std::shared_ptr<tasks::Callback> &callback) {
        std::shared_ptr<Listeners> listeners = getListeners(topic);
        std::shared_ptr<Listener> listener = listeners->addNewListener(callback);
        return listener;
    }

    data::ObjectAnchor PubSubManager::subscribe(
        data::ObjHandle scopeHandle,
        data::Symbol topic,
        const std::shared_ptr<tasks::Callback> &callback) {
        auto scope = scopeHandle.toObject<data::TrackingScope>();
        // if handle or root goes away, unsubscribe
        return scope->root()->anchor(subscribe(topic, callback));
    }

    void PubSubManager::insertTopicListenerSubTasks(
        std::shared_ptr<tasks::Task> &task, data::Symbol topic) {
        if(!topic) {
            // reserved for anonymous listeners
            return;
        }
        std::shared_ptr<Listeners> listeners = tryGetListeners(topic);
        if(listeners == nullptr || listeners->isEmpty()) {
            return;
        }
        std::vector<std::shared_ptr<Listener>> callOrder;
        listeners->fillTopicListeners(callOrder);
        for(const auto &i : callOrder) {
            task->addSubtask(std::move(i->toSubTask(topic)));
        }
    }

    void PubSubManager::initializePubSubCall(
        std::shared_ptr<tasks::Task> &task,
        const std::shared_ptr<Listener> &explicitListener,
        data::Symbol topic,
        const std::shared_ptr<data::StructModelBase> &dataIn,
        std::unique_ptr<tasks::SubTask> completion,
        tasks::ExpireTime expireTime) {
        if(!dataIn) {
            throw std::runtime_error("Data must be passed into an LPC call");
        }
        if(explicitListener) {
            task->addSubtask(std::move(explicitListener->toSubTask(topic)));
        }
        if(topic) {
            insertTopicListenerSubTasks(task, topic);
        }
        task->setData(dataIn);
        task->setCompletion(std::move(completion));
        task->setTimeout(expireTime);
    }

    void Listeners::fillTopicListeners(std::vector<std::shared_ptr<Listener>> &callOrder) {
        if(isEmpty()) {
            return;
        }
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

    std::unique_ptr<tasks::SubTask> Listener::toSubTask(const data::Symbol &topic) {
        std::shared_lock guard{context().lpcTopics().managerMutex()};
        std::unique_ptr<tasks::SubTask> subTask{std::make_unique<TopicSubTask>(topic, _callback)};
        return subTask;
    }

    std::shared_ptr<data::StructModelBase> Listener::runInTaskThread(
        const std::shared_ptr<tasks::Task> &task,
        const std::shared_ptr<data::StructModelBase> &dataIn) {
        assert(task->getSelf());
        assert(scope::thread().getActiveTask() == task);
        return _callback->invokeTopicCallback(task, _topic, dataIn);
    }

    std::shared_ptr<data::StructModelBase> TopicSubTask::runInThread(
        const std::shared_ptr<tasks::Task> &task,
        const std::shared_ptr<data::StructModelBase> &data) {
        return _callback->invokeTopicCallback(task, _topic, data);
    }
} // namespace pubsub
