#include "local_topics.hpp"
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
        data::Environment &environment,
        data::StringOrd topicOrd,
        Listeners *listeners,
        std::unique_ptr<AbstractCallback> callback
    )
        : data::TrackedObject{environment}, _topicOrd{topicOrd},
          _parent{listeners->weak_from_this()}, _callback{std::move(callback)} {
    }

    Listeners::Listeners(
        data::Environment &environment, data::StringOrd topicOrd, PubSubManager *topics
    )
        : _environment{environment}, _topicOrd{topicOrd}, _parent{topics->weak_from_this()} {
    }

    void Listeners::cleanup() {
        // scoped lock
        {
            std::unique_lock guard{_environment.sharedLocalTopicsMutex};
            (void) std::remove_if(_listeners.begin(), _listeners.end(), [&](const auto &item) {
                return item.expired();
            });
        }
        // lock must be released before this step
        if(_listeners.empty() && !_parent.expired()) {
            std::shared_ptr<PubSubManager> topics{_parent};
            topics->cleanup();
        }
    }

    void PubSubManager::cleanup() {
        std::unique_lock guard{_environment.sharedLocalTopicsMutex};
        for(auto i = _topics.begin(); i != _topics.end(); ++i) {
            if(i->second->isEmpty()) {
                _topics.erase(i);
            }
        }
    }

    std::shared_ptr<Listener> Listeners::addNewListener(std::unique_ptr<AbstractCallback> callback
    ) {
        std::shared_ptr<Listener> listener{
            std::make_shared<Listener>(_environment, _topicOrd, this, std::move(callback))};
        std::unique_lock guard{_environment.sharedLocalTopicsMutex};
        _listeners.push_back(listener);
        return listener;
    }

    std::shared_ptr<Listeners> PubSubManager::tryGetListeners(data::StringOrd topicOrd) {
        std::shared_lock guard{_environment.sharedLocalTopicsMutex};
        auto i = _topics.find(topicOrd);
        if(i == _topics.end()) {
            return {};
        } else {
            return i->second;
        }
    }

    std::shared_ptr<Listeners> PubSubManager::getListeners(data::StringOrd topicOrd) {
        std::shared_ptr<Listeners> listeners = tryGetListeners(topicOrd);
        if(listeners) {
            return listeners;
        }
        std::unique_lock guard{_environment.sharedLocalTopicsMutex};
        auto i = _topics.find(topicOrd);
        if(i != _topics.end()) {
            // rare edge case
            return i->second;
        }
        listeners = std::make_shared<Listeners>(_environment, topicOrd, this);
        _topics.emplace(topicOrd, listeners);
        return listeners;
    }

    std::shared_ptr<Listener> PubSubManager::subscribe(
        data::StringOrd topicOrd, std::unique_ptr<AbstractCallback> callback
    ) {
        std::shared_ptr<Listeners> listeners = getListeners(topicOrd);
        std::shared_ptr<Listener> listener = listeners->addNewListener(std::move(callback));
        return listener;
    }

    data::ObjectAnchor PubSubManager::subscribe(
        data::ObjHandle anchor, data::StringOrd topicOrd, std::unique_ptr<AbstractCallback> callback
    ) {
        auto root = _environment.handleTable.getObject<data::TrackingScope>(anchor);
        // if handle or root goes away, unsubscribe
        return root->anchor(subscribe(topicOrd, std::move(callback)));
    }

    void PubSubManager::insertTopicListenerSubTasks(
        std::shared_ptr<tasks::Task> &task, data::StringOrd topicOrd
    ) {
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
        data::StringOrd topic,
        const std::shared_ptr<data::StructModelBase> &dataIn,
        std::unique_ptr<tasks::SubTask> completion,
        tasks::ExpireTime expireTime
    ) {
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
        std::shared_lock guard{_environment.sharedLocalTopicsMutex};
        for(auto ri = _listeners.rbegin(); ri != _listeners.rend(); ++ri) {
            if(!ri->expired()) {
                callOrder.push_back(ri->lock());
            }
        }
    }

    class ListenerSubTask : public tasks::SubTask {
    private:
        std::shared_ptr<Listener> _listener;

    public:
        explicit ListenerSubTask(std::shared_ptr<Listener> &listener) : _listener{listener} {
        }

        std::shared_ptr<data::StructModelBase> runInThread(
            const std::shared_ptr<tasks::Task> &task,
            const std::shared_ptr<data::StructModelBase> &dataIn
        ) override;
    };

    std::unique_ptr<tasks::SubTask> Listener::toSubTask() {
        std::shared_lock guard{_environment.sharedLocalTopicsMutex};
        std::shared_ptr<Listener> listener{std::static_pointer_cast<Listener>(shared_from_this())};
        std::unique_ptr<tasks::SubTask> subTask{new ListenerSubTask(listener)};
        return subTask;
    }

    std::shared_ptr<data::StructModelBase> ListenerSubTask::runInThread(
        const std::shared_ptr<tasks::Task> &task,
        const std::shared_ptr<data::StructModelBase> &dataIn
    ) {
        return _listener->runInTaskThread(task, dataIn);
    }

    std::shared_ptr<data::StructModelBase> Listener::runInTaskThread(
        const std::shared_ptr<tasks::Task> &task,
        const std::shared_ptr<data::StructModelBase> &dataIn
    ) {
        data::ObjectAnchor anchor{task->anchor(dataIn)};
        data::Handle resp = _callback->operator()(task->getSelf(), _topicOrd, anchor.getHandle());
        std::shared_ptr<data::StructModelBase> respData;
        if(resp) {
            respData = _environment.handleTable.getObject<data::StructModelBase>(resp);
        }
        return respData;
    }

    std::shared_ptr<data::StructModelBase> CompletionSubTask::runInThread(
        const std::shared_ptr<tasks::Task> &task,
        const std::shared_ptr<data::StructModelBase> &result
    ) {
        data::ObjectAnchor anchor{task->anchor(result)};
        (void) _callback->operator()(task->getSelf(), _topicOrd, anchor.getHandle());
        return nullptr;
    }

    std::unique_ptr<tasks::SubTask> CompletionSubTask::of(
        data::StringOrd topicOrd, std::unique_ptr<AbstractCallback> callback
    ) {
        if(topicOrd && callback) {
            return std::unique_ptr<tasks::SubTask>{
                std::make_unique<CompletionSubTask>(topicOrd, std::move(callback))};
        } else {
            return {};
        }
    }
} // namespace pubsub
