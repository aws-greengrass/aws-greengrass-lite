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

uint32_t ggapiSendToTopic(uint32_t topicOrd, uint32_t callStruct, int32_t timeout) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([topicOrd, callStruct, timeout]() {
        data::Global &global = data::Global::self();
        data::Handle parentTask{tasks::Task::getThreadSelf()};
        std::shared_ptr<tasks::Task> parentTaskObj{
            global.environment.handleTable.getObject<tasks::Task>(parentTask)};
        data::ObjectAnchor taskAnchor{
            global.taskManager->createTask()}; // task is the anchor / return handle / context
        std::shared_ptr<tasks::Task> subTaskObj = taskAnchor.getObject<tasks::Task>();
        std::shared_ptr<data::StructModelBase> callDataStruct{
            global.environment.handleTable.getObject<data::StructModelBase>(data::ObjHandle{
                callStruct})};
        ExpireTime expireTime = global.environment.translateExpires(timeout);
        subTaskObj->setTimeout(expireTime);
        global.lpcTopics->insertCallQueue(subTaskObj, data::StringOrd{topicOrd});
        subTaskObj->setData(callDataStruct);
        global.taskManager->queueTask(subTaskObj); // task must be ready, any
                                                   // thread can pick up once
                                                   // queued
        // note, we don't allocate next worker, preferring to task-steal, but ok
        // if an idle worker picks it up
        if(subTaskObj->waitForCompletion(expireTime)) {
            return parentTaskObj->anchor(subTaskObj->getData()).getHandle().asInt();
        } else {
            return static_cast<uint32_t>(0);
        }
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
        data::Global &global = data::Global::self();
        data::ObjectAnchor taskAnchor{
            global.taskManager->createTask()}; // task is the anchor / return handle / context
        std::shared_ptr<tasks::Task> taskObject = taskAnchor.getObject<tasks::Task>();
        std::shared_ptr<data::StructModelBase> callDataStruct{
            global.environment.handleTable.getObject<data::StructModelBase>(data::ObjHandle{
                callStruct})};
        if(respCallback) {
            std::unique_ptr<pubsub::AbstractCallback> callback{
                new NativeCallback(respCallback, context)};
            taskObject->setCompletion(
                pubsub::CompletionSubTask::of(data::StringOrd{topicOrd}, std::move(callback))
            );
        }
        ExpireTime expireTime = global.environment.translateExpires(timeout);
        taskObject->setTimeout(expireTime);
        global.lpcTopics->insertCallQueue(taskObject, data::StringOrd{topicOrd});
        taskObject->setData(callDataStruct);
        global.taskManager->queueTask(taskObject); // task must be ready,
                                                   // any thread can pick
                                                   // up once queued
        global.taskManager->allocateNextWorker(); // this ensures a worker
                                                  // picks it up
        return taskAnchor.getHandle().asInt();
    });
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
