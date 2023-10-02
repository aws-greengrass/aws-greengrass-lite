#include "data/globals.h"
#include <cpp_api.h>

uint32_t ggapiClaimThread() noexcept {
    return ggapi::trapErrorReturn<uint32_t>([]() {
<<<<<<< HEAD
        data::Global &global = data::Global::self();
        std::shared_ptr<tasks::FixedTaskThread> thread{
            std::make_shared<tasks::FixedTaskThread>(global.environment, global.taskManager)};
=======
        data::Global & global = data::Global::self();
        if (ggapiGetCurrentTask() != 0) {
            throw std::runtime_error("Thread already claimed");
        }
        std::shared_ptr<tasks::FixedTaskThread> thread {std::make_shared<tasks::FixedTaskThread>(global.environment, global.taskManager)};
>>>>>>> 3fc2320 (Nucleus bootup-and-read-config procedure ported from GG-Java)
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

uint32_t ggapiWaitForTaskCompleted(uint32_t asyncTask, int32_t timeout) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([asyncTask, timeout]() {
        data::Global &global = data::Global::self();
        data::Handle parentTask{tasks::Task::getThreadSelf()};
        std::shared_ptr<tasks::Task> parentTaskObj{
            global.environment.handleTable.getObject<tasks::Task>(parentTask)};
        std::shared_ptr<tasks::Task> asyncTaskObj{
            global.environment.handleTable.getObject<tasks::Task>(data::ObjHandle{asyncTask})};
        ExpireTime expireTime = global.environment.translateExpires(timeout);
        if(asyncTaskObj->waitForCompletion(expireTime)) {
            return parentTaskObj->anchor(asyncTaskObj->getData()).getHandle().asInt();
        } else {
            return static_cast<uint32_t>(0);
        }
    });
}
