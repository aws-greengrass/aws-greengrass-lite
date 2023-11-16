#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <thread>

namespace scope {
    class Context;
}

namespace config {
    class Watcher;
    class Topics;

    using PublishAction = std::function<void()>;

    //
    // Publish Queue is a dedicated thread to handle configuration change publishes, in particular,
    // all config actions are strictly serialized when pushed to this queue
    //
    class PublishQueue {
        mutable std::mutex _mutex;
        std::weak_ptr<scope::Context> _context;
        std::thread _thread;
        std::list<PublishAction> _actions;
        std::condition_variable _wake;
        std::condition_variable _drained;
        std::atomic_bool _terminate{false};

    public:
        explicit PublishQueue(const std::shared_ptr<scope::Context> &context) : _context(context) {
        }
        void publish(PublishAction action);
        void start();
        void stop();
        void publishThread();
        std::optional<PublishAction> pickupAction();
        bool drainQueue();
    };
} // namespace config
