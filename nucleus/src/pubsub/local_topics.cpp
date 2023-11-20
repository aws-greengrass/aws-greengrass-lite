#include "local_topics.hpp"
#include "scope/context_full.hpp"
#include "tasks/task.hpp"
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
        std::unique_ptr<AbstractCallback> callback)
        : data::TrackedObject(context), _topicOrd(topicOrd), _parent(listeners->weak_from_this()),
          _callback(std::move(callback)) {
    }

    Listeners::Listeners(const std::shared_ptr<scope::Context> &context, data::Symbol topic)
        : _context(context), _topic(topic) {
    }

    void Listeners::cleanup() {
        // scoped lock
        {
            std::unique_lock guard{managerMutex()};
            std::ignore = std::remove_if(_listeners.begin(), _listeners.end(), [&](const auto &item) {
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
        std::unique_ptr<AbstractCallback> callback) {
        std::shared_ptr<Listener> listener{
            std::make_shared<Listener>(_context.lock(), _topic, this, std::move(callback))};
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
        data::Symbol topicOrd, std::unique_ptr<AbstractCallback> callback) {
        std::shared_ptr<Listeners> listeners = getListeners(topicOrd);
        std::shared_ptr<Listener> listener = listeners->addNewListener(std::move(callback));
        return listener;
    }

    data::ObjectAnchor PubSubManager::subscribe(
        data::ObjHandle scopeHandle,
        data::Symbol topicOrd,
        std::unique_ptr<AbstractCallback> callback) {
        auto scope = scopeHandle.toObject<data::TrackingScope>();
        // if handle or root goes away, unsubscribe
        return scope->root()->anchor(subscribe(topicOrd, std::move(callback)));
    }

    void PubSubManager::insertTopicListenerSubTasks(
        std::shared_ptr<tasks::Task> &task, data::Symbol topicOrd) {
        if(!topicOrd) {
            // reserved for anonymous listeners
            return;
        }
        std::shared_ptr<Listeners> listeners = tryGetListeners(topicOrd);
        if(listeners == nullptr || listeners->isEmpty()) {
            return;
        }
        std::vector<std::shared_ptr<Listener>> callOrder;
        listeners->fillTopicListeners(callOrder);
        for(const auto &i : callOrder) {
            task->addSubtask(std::move(i->toSubTask()));
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
            task->addSubtask(std::move(explicitListener->toSubTask()));
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

    class ListenerSubTask : public tasks::SubTask {
    private:
        std::shared_ptr<Listener> _listener;

    public:
        explicit ListenerSubTask(std::shared_ptr<Listener> &listener) : _listener{listener} {
        }

        std::shared_ptr<data::StructModelBase> runInThread(
            const std::shared_ptr<tasks::Task> &task,
            const std::shared_ptr<data::StructModelBase> &dataIn) override;
    };

    std::unique_ptr<tasks::SubTask> Listener::toSubTask() {
        std::shared_lock guard{context().lpcTopics().managerMutex()};
        std::shared_ptr<Listener> listener{ref<Listener>()};
        std::unique_ptr<tasks::SubTask> subTask{new ListenerSubTask(listener)};
        return subTask;
    }

    std::shared_ptr<data::StructModelBase> ListenerSubTask::runInThread(
        const std::shared_ptr<tasks::Task> &task,
        const std::shared_ptr<data::StructModelBase> &dataIn) {
        assert(scope::Context::thread().getActiveTask() == task); // sanity
        return _listener->runInTaskThread(task, dataIn);
    }

    std::shared_ptr<data::StructModelBase> Listener::runInTaskThread(
        const std::shared_ptr<tasks::Task> &task,
        const std::shared_ptr<data::StructModelBase> &dataIn) {
        assert(task->getSelf());
        assert(scope::Context::thread().getActiveTask() == task);

        auto scope = scope::Context::thread().getCallScope();

        data::ObjectAnchor anchor{scope->root()->anchor(dataIn)};
        data::ObjHandle resp =
            _callback->operator()(task->getSelf(), _topicOrd, anchor.getHandle());
        std::shared_ptr<data::StructModelBase> respData;
        if(resp) {
            respData = resp.toObject<data::StructModelBase>();
        }
        return respData;
    }

    std::shared_ptr<data::StructModelBase> CompletionSubTask::runInThread(
        const std::shared_ptr<tasks::Task> &task,
        const std::shared_ptr<data::StructModelBase> &result) {
        assert(scope::Context::thread().getActiveTask() == task); // sanity
        assert(task->getSelf()); // sanity
        auto scope = scope::Context::thread().getCallScope();
        auto anchor{scope->root()->anchor(result)};
        std::ignore = _callback->operator()(task->getSelf(), _topicOrd, anchor.getHandle());
        return nullptr;
    }

    std::unique_ptr<tasks::SubTask> CompletionSubTask::of(
        data::Symbol topicOrd, std::unique_ptr<AbstractCallback> callback) {
        if(topicOrd && callback) {
            return std::unique_ptr<tasks::SubTask>{
                std::make_unique<CompletionSubTask>(topicOrd, std::move(callback))};
        } else {
            return {};
        }
    }
} // namespace pubsub
