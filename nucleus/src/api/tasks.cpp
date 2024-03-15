#include "api_error_trap.hpp"
#include "scope/context_full.hpp"
#include "tasks/expire_time.hpp"
#include "tasks/task.hpp"
#include "tasks/task_callbacks.hpp"
#include <cpp_api.hpp>

/**
 * Create a task to call the provided callback at a future point in time.
 */
ggapiErrorKind ggapiCallAsync(ggapiObjHandle callbackHandle, uint32_t delay) noexcept {
    return apiImpl::catchErrorToKind([callbackHandle, delay]() {
        auto context = scope::context();
        if(!callbackHandle) {
            throw errors::CallbackError("Invalid callback handle");
        }
        auto callback = context->objFromInt<tasks::Callback>(callbackHandle);
        auto task = std::make_shared<tasks::AsyncCallbackTask>(callback);
        if(delay == 0) {
            context->taskManager().queueTask(task);
        } else {
            tasks::ExpireTime startTime = tasks::ExpireTime::fromNowMillis(delay);
            context->taskManager().queueTask(task, startTime);
        }
    });
}

ggapiErrorKind ggapiRegisterCallback(
    ::ggapiGenericCallback callbackFunction,
    ggapiContext callbackCtx,
    ggapiSymbol callbackType,
    ggapiObjHandle *pCallbackHandle) noexcept {

    return apiImpl::catchErrorToKind(
        [callbackFunction, callbackCtx, callbackType, pCallbackHandle]() {
            auto context = scope::context();
            auto module = scope::thread()->getEffectiveModule();
            auto typeSymbol = context->symbolFromInt(callbackType);
            std::shared_ptr<tasks::RegisteredCallback> callback =
                std::make_shared<tasks::RegisteredCallback>(
                    context, module, typeSymbol, callbackFunction, callbackCtx);
            *pCallbackHandle = scope::asIntHandle(callback);
        });
}
