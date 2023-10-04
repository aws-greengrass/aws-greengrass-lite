#include "local_topics.h"
#include "tasks/task.h"
#include <shared_mutex>

namespace pubsub {
    Listener::~Listener() {
        if(!_parent.expired()) {
            std::shared_ptr<Listeners> receivers{_parent};
            receivers->cleanup();
        }
    }

    Listener::Listener(
        data::Environment &environment,
        data::StringOrd topicOrd,
        Listeners *receivers,
        std::unique_ptr<AbstractCallback> &callback
    )
        : data::TrackedObject{environment}, _topicOrd{topicOrd},
          _parent{receivers->weak_from_this()}, _callback{std::move(callback)} {
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

    std::shared_ptr<Listener> Listeners::newReceiver(std::unique_ptr<AbstractCallback> &callback) {
        std::shared_ptr<Listener> receiver{
            std::make_shared<Listener>(_environment, _topicOrd, this, callback)};
        std::unique_lock guard{_environment.sharedLocalTopicsMutex};
        _listeners.push_back(receiver);
        return receiver;
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
        std::shared_ptr<Listeners> receivers = tryGetListeners(topicOrd);
        if(receivers) {
            return receivers;
        }
        std::unique_lock guard{_environment.sharedLocalTopicsMutex};
        auto i = _topics.find(topicOrd);
        if(i != _topics.end()) {
            // rare edge case
            return i->second;
        }
        receivers = std::make_shared<Listeners>(_environment, topicOrd, this);
        _topics[topicOrd] = receivers;
        return receivers;
    }

    data::ObjectAnchor PubSubManager::subscribe(
        data::ObjHandle anchor,
        data::StringOrd topicOrd,
        std::unique_ptr<AbstractCallback> &callback
    ) {
        auto root = _environment.handleTable.getObject<data::TrackingScope>(anchor);
        std::shared_ptr<Listeners> receivers = getListeners(topicOrd);
        std::shared_ptr<Listener> receiver = receivers->newReceiver(callback);
        return root->anchor(receiver); // if handle or root goes away, unsubscribe
    }

    void PubSubManager::insertCallQueue(
        std::shared_ptr<tasks::Task> &task, data::StringOrd topicOrd
    ) {
        std::shared_ptr<Listeners> receivers = tryGetListeners(topicOrd);
        if(receivers == nullptr || receivers->isEmpty()) {
            return;
        }
        std::vector<std::shared_ptr<Listener>> callOrder;
        receivers->getCallOrder(callOrder);
        for(const auto &i : callOrder) {
            task->addSubtask(std::move(i->toSubTask(task)));
        }
    }

    void Listeners::getCallOrder(std::vector<std::shared_ptr<Listener>> &callOrder) {
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

    class ReceiverSubTask : public tasks::SubTask {
    private:
        std::shared_ptr<Listener> _receiver;

    public:
        explicit ReceiverSubTask(std::shared_ptr<Listener> &receiver) : _receiver{receiver} {
        }

        std::shared_ptr<data::StructModelBase> runInThread(
            const std::shared_ptr<tasks::Task> &task,
            const std::shared_ptr<data::StructModelBase> &dataIn
        ) override;
    };

    std::unique_ptr<tasks::SubTask> Listener::toSubTask(std::shared_ptr<tasks::Task> &task) {
        std::shared_lock guard{_environment.sharedLocalTopicsMutex};
        std::shared_ptr<Listener> receiver{std::static_pointer_cast<Listener>(shared_from_this())};
        std::unique_ptr<tasks::SubTask> subTask{new ReceiverSubTask(receiver)};
        return subTask;
    }

    std::shared_ptr<data::StructModelBase> ReceiverSubTask::runInThread(
        const std::shared_ptr<tasks::Task> &task,
        const std::shared_ptr<data::StructModelBase> &dataIn
    ) {
        return _receiver->runInTaskThread(task, dataIn);
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
