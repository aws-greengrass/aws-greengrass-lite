#include "data/globals.hpp"
#include <cpp_api.hpp>

class NativeCallback : public pubsub::AbstractCallback {
private:
    ggapiTopicCallback _callback;
    uintptr_t _context;

public:
    explicit NativeCallback(ggapiTopicCallback callback, uintptr_t context)
        : _callback{callback}, _context{context} {
    }

    data::ObjHandle operator()(
        data::ObjHandle taskHandle, data::StringOrd topicOrd, data::ObjHandle dataStruct
    ) override {
        ::ggapiSetError(0);
        data::ObjHandle v = data::ObjHandle{
            _callback(_context, taskHandle.asInt(), topicOrd.asInt(), dataStruct.asInt())};
        if(!v) {
            data::StringOrd lastError{::ggapiGetError()};
            if(lastError) {
                ::ggapiSetError(0);
                throw pubsub::CallbackError(lastError);
            }
        }
        return v;
    }
};

bool ggapiIsSubscription(uint32_t handle) noexcept {
    return ggapi::trapErrorReturn<bool>([handle]() {
        data::Global &global = data::Global::self();
        auto ss{
            global.environment.handleTable.getObject<data::TrackedObject>(data::ObjHandle{handle})};
        return std::dynamic_pointer_cast<pubsub::Listener>(ss) != nullptr;
    });
}

uint32_t ggapiSubscribeToTopic(
    uint32_t anchorHandle, uint32_t topicOrd, ggapiTopicCallback rxCallback, uintptr_t context
) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([anchorHandle, topicOrd, rxCallback, context]() {
        data::Global &global = data::Global::self();
        std::unique_ptr<pubsub::AbstractCallback> callback{new NativeCallback(rxCallback, context)};
        return global.lpcTopics
            ->subscribe(data::ObjHandle{anchorHandle}, data::StringOrd{topicOrd}, callback)
            .getHandle()
            .asInt();
    });
}

static inline data::ObjHandle pubSubTaskCommon(
    data::ObjHandle listenerHandle,
    data::StringOrd topicOrd,
    data::ObjHandle callStructHandle,
    bool waitForCompletion,
    std::unique_ptr<tasks::SubTask> callback,
    int timeout
) {
    // Context
    data::Global &global = data::Global::self();
    std::shared_ptr<tasks::Task> parentTaskObj{
        global.environment.handleTable.getObject<tasks::Task>(tasks::Task::getThreadSelf())};

    // New Task
    auto newTaskAnchor{
        global.taskManager->createTask()}; // task is the anchor / return handle / context
    auto newTaskObj{newTaskAnchor.getObject<tasks::Task>()};

    // Fill out task
    std::shared_ptr<data::StructModelBase> callDataStruct{
        global.environment.handleTable.getObject<data::StructModelBase>(callStructHandle)};
    ExpireTime expireTime = global.environment.translateExpires(timeout);
    newTaskObj->setTimeout(expireTime);
    if(listenerHandle) {
        std::shared_ptr<pubsub::Listener> explicitReceiver =
            global.environment.handleTable.getObject<pubsub::Listener>(listenerHandle);
        if(explicitReceiver) {
            newTaskObj->addSubtask(std::move(explicitReceiver->toSubTask()));
        }
    }
    if(topicOrd) {
        global.lpcTopics->insertCallQueue(newTaskObj, topicOrd);
    }
    newTaskObj->setData(callDataStruct);
    newTaskObj->setCompletion(std::move(callback));

    // Schedule task
    global.taskManager->queueTask(newTaskObj);
    if(waitForCompletion) {
        // Synchronous version returns handle of return structure
        if(newTaskObj->waitForCompletion(expireTime)) {
            return parentTaskObj->anchor(newTaskObj->getData()).getHandle();
        } else {
            return {};
        }
    } else {
        // Async version returns handle of task
        global.taskManager->allocateNextWorker(); // this ensures a worker
                                                  // picks it up
        return newTaskAnchor.getHandle();
    }
}

uint32_t ggapiSendToTopic(uint32_t topicOrd, uint32_t callStruct, int32_t timeout) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([topicOrd, callStruct, timeout]() {
        return pubSubTaskCommon(
                   {}, data::StringOrd{topicOrd}, data::ObjHandle{callStruct}, true, {}, timeout
        )
            .asInt();
    });
}

uint32_t ggapiSendToListener(
    uint32_t listenerHandle, uint32_t callStruct, int32_t timeout
) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([listenerHandle, callStruct, timeout]() {
        return pubSubTaskCommon(
                   data::ObjHandle{listenerHandle},
                   {},
                   data::ObjHandle{callStruct},
                   true,
                   {},
                   timeout
        )
            .asInt();
    });
}

uint32_t ggapiSendToTopicAsync(
    uint32_t topicOrd,
    uint32_t callStruct,
    ggapiTopicCallback respCallback,
    uintptr_t context,
    int32_t timeout
) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([topicOrd, callStruct, respCallback, context, timeout](
                                            ) {
        std::unique_ptr<tasks::SubTask> callback;
        if(respCallback) {
            callback = pubsub::CompletionSubTask::of(
                data::StringOrd{topicOrd}, std::make_unique<NativeCallback>(respCallback, context)
            );
        }
        return pubSubTaskCommon(
                   {},
                   data::StringOrd{topicOrd},
                   data::ObjHandle{callStruct},
                   false,
                   std::move(callback),
                   timeout
        )
            .asInt();
    });
}

uint32_t ggapiSendToListenerAsync(
    uint32_t listenerHandle,
    uint32_t callStruct,
    ggapiTopicCallback respCallback,
    uintptr_t context,
    int32_t timeout
) noexcept {
    return ggapi::trapErrorReturn<uint32_t>(
        [listenerHandle, callStruct, respCallback, context, timeout]() {
            std::unique_ptr<tasks::SubTask> callback;
            if(respCallback) {
                callback = pubsub::CompletionSubTask::of(
                    {}, std::make_unique<NativeCallback>(respCallback, context)
                );
            }
            return pubSubTaskCommon(
                       data::ObjHandle{listenerHandle},
                       {},
                       data::ObjHandle{callStruct},
                       false,
                       std::move(callback),
                       timeout
            )
                .asInt();
        }
    );
}

//
// uint32_t ggapiCallNext(uint32_t dataStruct) {
//    Handle taskHandle { Task::getThreadSelf() };
//    if (!taskHandle) {
//        throw std::runtime_error("Expected thread to be associated with a
//        task");
//    }
//    auto taskObj {
//    global.environment.handleTable.getObject<Task>(taskHandle)}; if
//    (dataStruct) {
//        auto dataObj
//        {global.environment.handleTable.getObject<StructModelBase>(Handle{dataStruct})};
//        taskObj->setData(dataObj);
//    }
//    // task is permitted to run everything up to but not including
//    finalization
//    // and transition control back to this function
//    std::shared_ptr<StructModelBase> dataIn { taskObj->getData() };
//    std::shared_ptr<StructModelBase> dataOut {
//    taskObj->runInThreadCallNext(taskObj, dataIn) }; return
//    Handle{taskObj->anchor(dataOut.get())}.asInt();
//}
