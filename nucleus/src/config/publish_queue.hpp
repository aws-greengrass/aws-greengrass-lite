#pragma once
#include "channel/channel_base.hpp"
#include "scope/context.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <thread>

namespace config {
    class Watcher;
    class Topics;

    using PublishAction = std::function<void()>;

    namespace traits {
        struct PublishQueueStub {};
        struct PublishQueueTraits {
            using DataType = PublishAction;
            using ListenCallbackType = PublishQueueStub;
            using CloseCallbackType = PublishQueueStub;
            using IdleCallbackType = PublishQueueStub;
            static void invokeListenCallback(const ListenCallbackType &, const DataType &action) {
                action();
            }
            static void invokeCloseCallback(const CloseCallbackType &) {
            }
            static void onIdle(const IdleCallbackType &) {
            }
        };
    } // namespace traits

    //
    // Publish Queue is a dedicated channel to manage sequence of items to be published
    //
    class PublishQueue : public util::RefObject<PublishQueue>, protected scope::UsesContext {
        std::shared_ptr<channel::ChannelBase<traits::PublishQueueTraits>> _channel;

    public:
        explicit PublishQueue(const scope::UsingContext &context);
        void publish(PublishAction action);
        void start();
        void stop();
        void drainQueue();
    };

} // namespace config
