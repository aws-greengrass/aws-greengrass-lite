#include "scope/context_full.hpp"
#include "tasks/expire_time.hpp"
#include "tasks/task.hpp"
#include "tasks/task_callbacks.hpp"
#include "tasks/task_threads.hpp"
#include <cpp_api.hpp>

uint32_t ggapiGetCurrentTask() noexcept {
    return ggapi::trapErrorReturn<uint32_t>(
        []() { return scope::Context::thread().getActiveTask()->getSelf().asInt(); });
}

bool ggapiIsTask(uint32_t handle) noexcept {
    return ggapi::trapErrorReturn<bool>([handle]() {
        auto ss{scope::context().objFromInt(handle)};
        return std::dynamic_pointer_cast<tasks::Task>(ss) != nullptr;
    });
}

//
// Cause the current thread to block waiting on the provided task, either until it has
// completed, or cancelled (including time-out).
//
uint32_t ggapiWaitForTaskCompleted(uint32_t asyncTask, int32_t timeout) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([asyncTask, timeout]() {
        auto &context = scope::context();
        std::shared_ptr<tasks::Task> asyncTaskObj{
            context.handleFromInt(asyncTask).toObject<tasks::Task>()};
        tasks::ExpireTime expireTime = tasks::ExpireTime::infinite(); // negative values
        if(timeout >= 0) {
            expireTime = tasks::ExpireTime::fromNowMillis(timeout);
        }
        if(asyncTaskObj->waitForCompletion(expireTime)) {
            return scope::NucleusCallScopeContext::anchor(asyncTaskObj->getData()).asIntHandle();
        } else {
            return static_cast<uint32_t>(0);
        }
    });
}

//
// Cause the current thread to block for determined length of time, while allowing thread to be
// used to steal tasks.
//
bool ggapiSleep(uint32_t duration) noexcept {
    return ggapi::trapErrorReturn<bool>([duration]() {
        auto &tc = scope::thread();
        tasks::ExpireTime expireTime = tasks::ExpireTime::fromNowMillis(duration);
        tc.getThreadTaskData()->sleep(expireTime);
        return true;
    });
}

//
// Cause specified task to exit immediately if/when idle. If specified task is executing a sub-task
// the sub-task will be completed first. Consider this function to cancel any and all
// 'ggapiWaitForTaskCompleted' operations on the given task.
//
bool ggapiCancelTask(uint32_t asyncTask) noexcept {
    return ggapi::trapErrorReturn<bool>([asyncTask]() {
        scope::context().objFromInt<tasks::Task>(asyncTask)->cancelTask();
        return true;
    });
}

/**
 * Annotate the thread to mark it as "single thread" mode - this causes affinities to be
 * automatically assigned to callbacks.
 */
bool ggapiSetSingleThread(bool enable) noexcept {
    return ggapi::trapErrorReturn<bool>([enable]() {
        // Applies to current thread
        scope::thread().getThreadTaskData()->setSingleThreadMode(enable);
        return true;
    });
}

/**
 * Create a task to call the provided callback at a future point in time.
 */
uint32_t ggapiCallAsync(uint32_t callStruct, uint32_t callbackHandle, uint32_t delay) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([callStruct, callbackHandle, delay]() {
        auto &context = scope::context();
        if(!callbackHandle) {
            throw errors::CallbackError("Invalid callback handle");
        }
        auto callback = context.objFromInt<tasks::Callback>(callbackHandle);
        auto callDataStruct = context.objFromInt<data::StructModelBase>(callStruct);
        auto subTask = std::make_unique<tasks::SimpleSubTask>(callback);
        auto taskObj = std::make_shared<tasks::Task>(context.baseRef());
        tasks::ExpireTime startTime = tasks::ExpireTime::fromNowMillis(delay);
        taskObj->addSubtask(std::move(subTask));
        taskObj->setData(callDataStruct);
        taskObj->setStartTime(startTime);
        auto threadTaskData = scope::thread().getThreadTaskData();
        if(threadTaskData->isSingleThreadMode()) {
            // If single thread mode, then specify preferred thread for callbacks
            taskObj->setDefaultThread(threadTaskData);
        }
        context.taskManager().queueTask(taskObj);
        return taskObj->getSelf().asInt();
    });
}

uint32_t ggapiRegisterCallback(
    ::ggapiGenericCallback callbackFunction,
    uintptr_t callbackCtx,
    uint32_t callbackType) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([callbackFunction, callbackCtx, callbackType]() {
        auto &context = scope::context();
        auto module = scope::thread().getEffectiveModule();
        auto typeSymbol = context.symbolFromInt(callbackType);
        std::shared_ptr<tasks::RegisteredCallback> callback =
            std::make_shared<tasks::RegisteredCallback>(
                context.baseRef(), module, typeSymbol, callbackFunction, callbackCtx);
        return scope::NucleusCallScopeContext::anchor(callback).asIntHandle();
    });
}
