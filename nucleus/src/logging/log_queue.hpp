#pragma once

#include "scope/context.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>

namespace data {
    class StructModelBase;
}

namespace logging {
    class LogState;

    /**
     * LogQueue is a dedicated thread to handle log publishes, in particular,
     * all log entries are strictly serialized when pushed to this queue
     */
    // NOLINTNEXTLINE(*-special-member-functions)
    class LogQueue : private scope::UsesContext {
    public:
        using QueueEntry =
            std::pair<std::shared_ptr<LogState>, std::shared_ptr<data::StructModelBase>>;

    private:
        mutable std::mutex _mutex;
        mutable std::mutex _drainMutex;
        std::thread _thread;
        std::list<QueueEntry> _entries;
        std::condition_variable _wake;
        std::condition_variable _drained;
        std::atomic_bool _running{false};
        std::atomic_bool _terminate{false};
        std::atomic_bool _watching{false};
        std::function<bool(const QueueEntry &entry)> _watch;
        std::unordered_set<std::string> _needsSync;

    public:
        using scope::UsesContext::UsesContext;
        ~LogQueue() noexcept;
        void publish(std::shared_ptr<LogState> state, std::shared_ptr<data::StructModelBase> entry);
        void reconfigure(const std::shared_ptr<LogState> &state);
        std::optional<QueueEntry> pickupEntry();
        void processEntry(const QueueEntry &entry);
        void setWatch(std::function<bool(const QueueEntry &entry)> fn);
        void stop();
        void publishThread();
        bool drainQueue();
        void syncOutputs();
    };
} // namespace logging
