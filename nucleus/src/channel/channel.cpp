#include "channel_base.hpp"
#include "scope/context_full.hpp"

namespace channel {
    void ChannelBaseCore::channelWork(
        const std::shared_ptr<channel::ChannelBaseCore> &keepInMemory,
        const std::shared_ptr<data::StructModelBase> &) {

        // We pass in this parameter to keep a ref-count to channel until this task completes
        std::ignore = keepInMemory;

        std::unique_lock lock(_mutex);
        while(canProcessWork(lock)) {
            doReceiveWork(lock);
            // There may still be work to do
            if(!isEmpty(lock)) {
                continue;
            }
            doIdle(lock);
            // More work may have been injected if/when we released lock during idle callback
            if(!isEmpty(lock)) {
                continue;
            }
            doCloseWork(lock);
            // More work may have been injected if/when we released lock during close work
        }
        assert(lock.owns_lock());
        // TODO: missing RII behavior
        _pendingTask.reset();
    }

    bool ChannelBaseCore::doIdle(std::unique_lock<std::mutex> &lock) const {
        assert(lock.owns_lock());
        lock.unlock();
    }

    void ChannelBaseCore::queueWork(std::unique_lock<std::mutex> &lock) {
        assert(lock.owns_lock());
        if(!_pendingTask) {
            return; // Task already queued or in progress
        }
        if(!canProcessWork(lock)) {
            return; // Cannot process work yet
        }
        auto ctx = context();
        _pendingTask = ctx->taskManager().callAsync(
            &ChannelBaseCore::channelWork, this, ref<ChannelBaseCore>());
    }

    bool ChannelBaseCore::drain() {
        std::unique_lock lock(_mutex);
        queueWork(lock);
        auto task = _pendingTask;
        if(!task) {
            return isEmpty(lock);
        }
        // If we are here, pending task will not have entered its critical section, therefore
        // we know we can drain everything that was pending up until this point by simply waiting
        // for the pending task to complete
        lock.unlock();
        task->waitForCompletion(tasks::ExpireTime::infinite());
        return true;
    }

} // namespace channel