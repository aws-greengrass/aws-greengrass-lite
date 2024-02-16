#include "errors/error_base.hpp"
#include "pubsub/local_topics.hpp"
#include "scope/context_full.hpp"
#include "tasks/expire_time.hpp"
#include "tasks/task.hpp"
#include "tasks/task_callbacks.hpp"
#include "tasks/task_threads.hpp"
#include <cpp_api.hpp>

extern "C" bool ggapiIsSubscription(uint32_t handle) noexcept {
    return ggapi::trapErrorReturn<bool>([handle]() {
        auto ss{scope::context()->objFromInt(handle)};
        return std::dynamic_pointer_cast<pubsub::Listener>(ss) != nullptr;
    });
}

extern "C" uint32_t ggapiSubscribeToTopic(
    uint32_t anchorHandle, uint32_t topic, uint32_t rxCallback) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([anchorHandle, topic, rxCallback]() {
        auto context = scope::context();
        auto callback = context->objFromInt<tasks::Callback>(rxCallback);
        return context->lpcTopics()
            .subscribe(
                context->handleFromInt(anchorHandle), context->symbolFromInt(topic), callback)
            .asIntHandle();
    });
}

static inline std::shared_ptr<tasks::Task> pubSubCreateCommon(
    data::ObjHandle listenerHandle,
    data::Symbol topic,
    data::ObjHandle callStructHandle,
    std::unique_ptr<tasks::SubTask> callback,
    int timeout) {
    auto context = scope::context();

    // New Task
    auto newTaskObj = std::make_shared<tasks::Task>(context);

    // Fill out task
    std::shared_ptr<pubsub::Listener> explicitListener;
    if(listenerHandle) {
        explicitListener = listenerHandle.toObject<pubsub::Listener>();
    }
    std::shared_ptr<data::StructModelBase> callDataStruct{
        callStructHandle.toObject<data::StructModelBase>()};
    tasks::ExpireTime expireTime = tasks::ExpireTime::infinite(); // negative values
    if(timeout >= 0) {
        expireTime = tasks::ExpireTime::fromNowMillis(timeout);
    }

    context->lpcTopics().initializePubSubCall(
        newTaskObj, explicitListener, topic, callDataStruct, std::move(callback), expireTime);
    return newTaskObj;
}

static inline data::ObjHandle pubSubQueueAndWaitCommon(
    const std::shared_ptr<tasks::Task> &taskObj) {
    // Behave as if single thread mode
    taskObj->setDefaultThread(tasks::TaskThread::getThreadContext());
    scope::context()->taskManager().queueTask(taskObj);
    if(taskObj->wait()) {
        // Return data needs to be anchored
        return scope::NucleusCallScopeContext::anchor(taskObj->getData()).getHandle();
    } else {
        return {};
    }
}

static inline data::ObjHandle pubSubQueueAsyncCommon(const std::shared_ptr<tasks::Task> &taskObj) {
    scope::context()->taskManager().queueTask(taskObj);
    auto threadTaskData = scope::thread()->getThreadTaskData();
    if(threadTaskData->isSingleThreadMode()) {
        // If single thread mode, then specify preferred thread for callbacks
        taskObj->setDefaultThread(threadTaskData);
    }
    return taskObj->getSelf();
}

extern "C" uint32_t ggapiSendToTopic(
    uint32_t topic, uint32_t callStruct, int32_t timeout) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([topic, callStruct, timeout]() {
        auto context = scope::context();
        std::shared_ptr<tasks::Task> taskObj = pubSubCreateCommon(
            {}, context->symbolFromInt(topic), context->handleFromInt(callStruct), {}, timeout);
        return pubSubQueueAndWaitCommon(taskObj).asInt();
    });
}

extern "C" uint32_t ggapiSendToListener(
    uint32_t listenerHandle, uint32_t callStruct, int32_t timeout) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([listenerHandle, callStruct, timeout]() {
        auto context = scope::context();
        std::shared_ptr<tasks::Task> taskObj = pubSubCreateCommon(
            context->handleFromInt(listenerHandle),
            {},
            context->handleFromInt(callStruct),
            {},
            timeout);
        return pubSubQueueAndWaitCommon(taskObj).asInt();
    });
}

extern "C" uint32_t ggapiSendToTopicAsync(
    uint32_t topic, uint32_t callStruct, uint32_t respCallback, int32_t timeout) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([topic, callStruct, respCallback, timeout]() {
        auto context = scope::context();
        auto callback = context->objFromInt<tasks::Callback>(respCallback);
        std::unique_ptr<tasks::SubTask> respSubTask;
        if(callback) {
            respSubTask =
                std::make_unique<pubsub::TopicSubTask>(context->symbolFromInt(topic), callback);
        }
        std::shared_ptr<tasks::Task> taskObj = pubSubCreateCommon(
            {},
            context->symbolFromInt(topic),
            context->handleFromInt(callStruct),
            std::move(respSubTask),
            timeout);
        return pubSubQueueAsyncCommon(taskObj).asInt();
    });
}

extern "C" uint32_t ggapiSendToListenerAsync(
    uint32_t listenerHandle, uint32_t callStruct, uint32_t respCallback, int32_t timeout) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([listenerHandle, callStruct, respCallback, timeout]() {
        auto context = scope::context();
        auto callback = context->objFromInt<tasks::Callback>(respCallback);
        std::unique_ptr<tasks::SubTask> respSubTask;
        if(callback) {
            respSubTask = std::make_unique<pubsub::TopicSubTask>(data::Symbol{}, callback);
        }
        std::shared_ptr<tasks::Task> taskObj = pubSubCreateCommon(
            context->handleFromInt(listenerHandle),
            {},
            context->handleFromInt(callStruct),
            std::move(respSubTask),
            timeout);
        return pubSubQueueAsyncCommon(taskObj).asInt();
    });
}
