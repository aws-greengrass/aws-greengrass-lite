#include "scope/context_full.hpp"
#include "tasks/expire_time.hpp"
#include "tasks/task.hpp"
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
        auto &context = scope::Context::get();
        std::shared_ptr<tasks::Task> asyncTaskObj{
            context.handleFromInt(asyncTask).toObject<tasks::Task>()};
        tasks::ExpireTime expireTime = tasks::ExpireTime::fromNowMillis(timeout);
        if(asyncTaskObj->waitForCompletion(expireTime)) {
            return scope::NucleusCallScopeContext::anchor(asyncTaskObj->getData()).asIntHandle();
        } else {
            return static_cast<uint32_t>(0);
        }
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
