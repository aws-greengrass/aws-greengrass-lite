#pragma once
#include "channel_base.hpp"
#include "tasks/task_callbacks.hpp"

namespace tasks {
    class Callback;
    class Task;
    class ExpireTime;
}

namespace channel {

    namespace traits {
        struct ChannelTraits {
            struct IdleCallbackStub {};
            using DataType = std::shared_ptr<data::TrackedObject>;
            using ListenCallbackType = std::shared_ptr<tasks::Callback>;
            using CloseCallbackType = std::shared_ptr<tasks::Callback>;
            using IdleCallbackType = IdleCallbackStub;
            static void invokeListenCallback(
                const ListenCallbackType &callback, const DataType &data) {
                if(callback) {
                    callback->invokeChannelListenCallback(data);
                }
            }
            static void invokeCloseCallback(const CloseCallbackType &callback) {
                if(callback) {
                    callback->invokeChannelCloseCallback();
                }
            }
            static void onIdle(const IdleCallbackType &) {
            }
        };
    } // namespace traits

    /**
     * Stream of objects with standard callbacks
     */
    class Channel final : public ChannelBase<std::shared_ptr<data::TrackedObject>> {
    private:
        std::optional<std::shared_ptr<tasks::Callback>> _listener;
        std::vector<std::shared_ptr<tasks::Callback>> _onClose;
    public:

    };

} // namespace channel
