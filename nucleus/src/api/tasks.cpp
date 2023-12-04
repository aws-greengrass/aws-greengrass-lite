#include "scope/context_full.hpp"
#include "tasks/expire_time.hpp"
#include "tasks/task.hpp"
#include "tasks/task_threads.hpp"
#include <cpp_api.hpp>

class SimpleSubTask : public tasks::SubTask {
private:
    ggapiTaskCallback _callback;
    uintptr_t _context;

public:
    explicit SimpleSubTask(ggapiTaskCallback callback, uintptr_t context)
        : _callback{callback}, _context{context} {
    }

    std::shared_ptr<data::StructModelBase> runInThread(
        const std::shared_ptr<tasks::Task> &task,
        const std::shared_ptr<data::StructModelBase> &dataIn) override {
        assert(scope::thread().getActiveTask() == task); // sanity
        assert(task->getSelf()); // sanity
        auto scope = scope::thread().getCallScope();
        errors::ThreadErrorContainer::get().clear();
        data::ObjectAnchor anchor{scope->root()->anchor(dataIn)};
        auto flag = _callback(_context, anchor.asIntHandle());
        if(!flag) {
            errors::ThreadErrorContainer::get().throwIfError();
        }
        return {};
    }
};

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
uint32_t ggapiCallAsync(
    uint32_t callStruct,
    ggapiTaskCallback futureCallback,
    uintptr_t callbackCtx,
    uint32_t delay) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([callStruct, futureCallback, callbackCtx, delay]() {
        auto &context = scope::context();
        std::unique_ptr<tasks::SubTask> callback;
        if(!futureCallback) {
            throw errors::InvalidCallbackError();
        }
        auto callDataStruct = context.objFromInt<data::StructModelBase>(callStruct);
        callback = std::make_unique<SimpleSubTask>(futureCallback, callbackCtx);
        auto taskObj = std::make_shared<tasks::Task>(context.baseRef());
        tasks::ExpireTime startTime = tasks::ExpireTime::fromNowMillis(delay);
        taskObj->addSubtask(std::move(callback));
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
