#pragma once
#include "channel/channel_base.hpp"
#include "data/handle_table.hpp"
#include "data/safe_handle.hpp"
#include "data/shared_struct.hpp"
#include "scope/context.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>

namespace logging {
    class LogState;
    class LogQueue;

    using LogQueueEntry =
        std::pair<std::shared_ptr<LogState>, std::shared_ptr<data::StructModelBase>>;

    class LogQueueCallbacks {
        std::weak_ptr<LogQueue> _ptr;

    public:
        void processEntry(const LogQueueEntry &entry) const;
        void onIdle() const;
    };

    namespace traits {
        struct PublishQueueTraits {
            using DataType = LogQueueEntry;
            using ListenCallbackType = LogQueueCallbacks;
            using CloseCallbackType = LogQueueCallbacks;
            using IdleCallbackType = LogQueueCallbacks;
            static void invokeListenCallback(
                const ListenCallbackType &cb, const LogQueueEntry &action) {
                cb.processEntry(action);
            }
            static void invokeCloseCallback(const CloseCallbackType &) {
            }
            static void onIdle(const IdleCallbackType &cb) {
                cb.onIdle();
            }
        };
    } // namespace traits

    /**
     * LogQueue is a dedicated channel to handle log publishes, in particular,
     * all log entries are strictly serialized when pushed to this queue
     */
    // NOLINTNEXTLINE(*-special-member-functions)
    class LogQueue : private scope::UsesContext {
    private:
        //        mutable std::mutex _mutex;
        //        std::thread _thread;
        //        std::list<QueueEntry> _entries;
        //        std::condition_variable _wake;
        //        std::condition_variable _drained;
        //        std::atomic_bool _running{false};
        //        std::atomic_bool _terminate{false};
        //        std::atomic_bool _watching{false};
        //        std::function<bool(const QueueEntry &entry)> _watch;
        std::shared_ptr<channel::ChannelBase<traits::PublishQueueTraits>> _channel;
        std::unordered_set<std::string> _needsSync;

    public:
        explicit LogQueue(const scope::UsingContext &context)
            : scope::UsesContext(context), _channel {
        }
        ~LogQueue();
        void publish(
            const std::shared_ptr<LogState> &state,
            const std::shared_ptr<data::StructModelBase> &entry);
        void reconfigure(const std::shared_ptr<LogState> &state);
        std::optional<QueueEntry> pickupEntry();
        void processEntry(const QueueEntry &entry);
        void setWatch(const std::function<bool(const QueueEntry &entry)> &fn);
        void stop();
        void publishThread();
        bool drainQueue();
        void syncOutputs();
    };
} // namespace logging
