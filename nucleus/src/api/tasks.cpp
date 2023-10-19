#include "data/globals.hpp"
#include "tasks/expire_time.hpp"
#include <cpp_api.hpp>

uint32_t ggapiClaimThread() noexcept {
    return ggapi::trapErrorReturn<uint32_t>([]() {
        data::Global &global = data::Global::self();
        if(ggapiGetCurrentTask() != 0) {
            throw std::runtime_error("Thread already claimed");
        }
        std::shared_ptr<tasks::FixedTaskThread> thread{
            std::make_shared<tasks::FixedTaskThread>(global.environment, global.taskManager)};
        return thread->claimFixedThread().getHandle().asInt();
    });
}

bool ggapiReleaseThread() noexcept {
    return ggapi::trapErrorReturn<bool>([]() {
        std::shared_ptr<tasks::TaskThread> thread = tasks::FixedTaskThread::getThreadContext();
        thread->releaseFixedThread();
        return true;
    });
}

uint32_t ggapiGetCurrentTask() noexcept {
    return ggapi::trapErrorReturn<uint32_t>([]() { return tasks::Task::getThreadSelf().asInt(); });
}

//
// Cause the current thread to block waiting on the provided task, either until it has
// completed, or cancelled (including time-out).
//
uint32_t ggapiWaitForTaskCompleted(uint32_t asyncTask, int32_t timeout) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([asyncTask, timeout]() {
        data::Global &global = data::Global::self();
        data::Handle parentTask{tasks::Task::getThreadSelf()};
        std::shared_ptr<tasks::Task> parentTaskObj{
            global.environment.handleTable.getObject<tasks::Task>(parentTask)};
        std::shared_ptr<tasks::Task> asyncTaskObj{
            global.environment.handleTable.getObject<tasks::Task>(data::ObjHandle{asyncTask})};
        tasks::ExpireTime expireTime = global.environment.translateExpires(timeout);
        if(asyncTaskObj->waitForCompletion(expireTime)) {
            return parentTaskObj->anchor(asyncTaskObj->getData()).getHandle().asInt();
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
        data::Global &global = data::Global::self();
        std::shared_ptr<tasks::Task> asyncTaskObj{
            global.environment.handleTable.getObject<tasks::Task>(data::ObjHandle{asyncTask})};
        asyncTaskObj->cancelTask();
        return true;
    });
}
