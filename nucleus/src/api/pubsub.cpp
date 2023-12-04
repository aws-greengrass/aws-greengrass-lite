#include "errors/error_base.hpp"
#include "pubsub/local_topics.hpp"
#include "scope/context_full.hpp"
#include "tasks/expire_time.hpp"
#include "tasks/task.hpp"
#include "tasks/task_threads.hpp"
#include <cpp_api.hpp>

class NativeTopicCallback : public pubsub::AbstractCallback {
private:
    ggapiTopicCallback _callback;
    uintptr_t _context;

public:
    explicit NativeTopicCallback(ggapiTopicCallback callback, uintptr_t context)
        : _callback{callback}, _context{context} {
    }

    data::ObjHandle operator()(
        data::ObjHandle taskHandle, data::Symbol topicOrd, data::ObjHandle argsHandle) override {
        auto &context = scope::Context::get();
        errors::ThreadErrorContainer::get().clear();
        auto resIntHandle =
            _callback(_context, taskHandle.asInt(), topicOrd.asInt(), argsHandle.asInt());
        data::ObjHandle v = context.handleFromInt(resIntHandle);
        if(!v) {
            errors::ThreadErrorContainer::get().throwIfError();
        }
        return v;
    }
};

bool ggapiIsSubscription(uint32_t handle) noexcept {
    return ggapi::trapErrorReturn<bool>([handle]() {
        auto ss{scope::context().objFromInt(handle)};
        return std::dynamic_pointer_cast<pubsub::Listener>(ss) != nullptr;
    });
}

uint32_t ggapiSubscribeToTopic(
    uint32_t anchorHandle,
    uint32_t topicOrd,
    ggapiTopicCallback rxCallback,
    uintptr_t contextInt) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([anchorHandle, topicOrd, rxCallback, contextInt]() {
        auto &context = scope::context();
        auto scope = context.objFromInt<data::TrackingScope>(anchorHandle);
        if(!scope) {
            throw errors::NullHandleError();
        }
        if(!rxCallback) {
            throw errors::InvalidCallbackError();
        }
        auto threadTaskData = scope::thread().getThreadTaskData();
        std::shared_ptr<tasks::TaskThread> affinity;
        if(threadTaskData->isSingleThreadMode()) {
            affinity = threadTaskData;
        }
        std::unique_ptr<pubsub::AbstractCallback> callback{
            new NativeTopicCallback(rxCallback, contextInt)};
        auto anchor = scope->root()->anchor(context.lpcTopics().subscribe(
            context.symbolFromInt(topicOrd), std::move(callback), affinity));
        return anchor.asIntHandle();
    });
}

static inline std::shared_ptr<tasks::Task> pubSubCreateCommon(
    data::ObjHandle listenerHandle,
    data::Symbol topicOrd,
    data::ObjHandle callStructHandle,
    std::unique_ptr<tasks::SubTask> callback,
    int timeout) {
    auto &context = scope::Context::get();

    // New Task
    auto newTaskObj = std::make_shared<tasks::Task>(context.baseRef());

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

    context.lpcTopics().initializePubSubCall(
        newTaskObj, explicitListener, topicOrd, callDataStruct, std::move(callback), expireTime);
    return newTaskObj;
}

static inline data::ObjHandle pubSubQueueAndWaitCommon(
    const std::shared_ptr<tasks::Task> &taskObj) {
    // Behave as if single thread mode
    taskObj->setDefaultThread(tasks::TaskThread::getThreadContext());
    scope::context().taskManager().queueTask(taskObj);
    if(taskObj->wait()) {
        // Return data needs to be anchored
        return scope::NucleusCallScopeContext::anchor(taskObj->getData()).getHandle();
    } else {
        return {};
    }
}

static inline data::ObjHandle pubSubQueueAsyncCommon(const std::shared_ptr<tasks::Task> &taskObj) {
    scope::context().taskManager().queueTask(taskObj);
    auto threadTaskData = scope::thread().getThreadTaskData();
    if(threadTaskData->isSingleThreadMode()) {
        // If single thread mode, then specify preferred thread for callbacks
        taskObj->setDefaultThread(threadTaskData);
    }
    return taskObj->getSelf();
}

uint32_t ggapiSendToTopic(uint32_t topicOrd, uint32_t callStruct, int32_t timeout) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([topicOrd, callStruct, timeout]() {
        auto &context = scope::Context::get();
        std::shared_ptr<tasks::Task> taskObj = pubSubCreateCommon(
            {}, context.symbolFromInt(topicOrd), context.handleFromInt(callStruct), {}, timeout);
        return pubSubQueueAndWaitCommon(taskObj).asInt();
    });
}

uint32_t ggapiSendToListener(
    uint32_t listenerHandle, uint32_t callStruct, int32_t timeout) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([listenerHandle, callStruct, timeout]() {
        auto &context = scope::Context::get();
        std::shared_ptr<tasks::Task> taskObj = pubSubCreateCommon(
            context.handleFromInt(listenerHandle),
            {},
            context.handleFromInt(callStruct),
            {},
            timeout);
        return pubSubQueueAndWaitCommon(taskObj).asInt();
    });
}

uint32_t ggapiSendToTopicAsync(
    uint32_t topicOrd,
    uint32_t callStruct,
    ggapiTopicCallback respCallback,
    uintptr_t callbackCtx,
    int32_t timeout) noexcept {
    return ggapi::trapErrorReturn<uint32_t>(
        [topicOrd, callStruct, respCallback, callbackCtx, timeout]() {
            auto &context = scope::Context::get();
            std::unique_ptr<tasks::SubTask> callback;
            if(respCallback) {
                callback = pubsub::CompletionSubTask::of(
                    context.symbolFromInt(topicOrd),
                    std::make_unique<NativeTopicCallback>(respCallback, callbackCtx));
            }
            std::shared_ptr<tasks::Task> taskObj = pubSubCreateCommon(
                {},
                context.symbolFromInt(topicOrd),
                context.handleFromInt(callStruct),
                std::move(callback),
                timeout);
            return pubSubQueueAsyncCommon(taskObj).asInt();
        });
}

uint32_t ggapiSendToListenerAsync(
    uint32_t listenerHandle,
    uint32_t callStruct,
    ggapiTopicCallback respCallback,
    uintptr_t callbackCtx,
    int32_t timeout) noexcept {
    return ggapi::trapErrorReturn<uint32_t>(
        [listenerHandle, callStruct, respCallback, callbackCtx, timeout]() {
            auto &context = scope::Context::get();
            std::unique_ptr<tasks::SubTask> callback;
            if(respCallback) {
                callback = pubsub::CompletionSubTask::of(
                    {}, std::make_unique<NativeTopicCallback>(respCallback, callbackCtx));
            }
            std::shared_ptr<tasks::Task> taskObj = pubSubCreateCommon(
                context.handleFromInt(listenerHandle),
                {},
                context.handleFromInt(callStruct),
                std::move(callback),
                timeout);
            return pubSubQueueAsyncCommon(taskObj).asInt();
        });
}
