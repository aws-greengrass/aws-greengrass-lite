#pragma once
#include "data/handle_table.hpp"
#include "data/safe_handle.hpp"
#include "data/shared_struct.hpp"
#include "data/string_table.hpp"
#include "data/symbol_value_map.hpp"
#include "errors/error_base.hpp"
#include "scope/context.hpp"
#include "tasks/task.hpp"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace tasks {
    class Callback;
    class Task;
    class SubTask;
} // namespace tasks

namespace data {
    class StructModelBase;
}

namespace scope {
    class Context;
}

namespace pubsub {
    class Listeners;
    class PubSubManager;

    class TopicSubTask : public tasks::SubTask {
    private:
        data::Symbol _topic;
        std::shared_ptr<tasks::Callback> _callback;

    public:
        explicit TopicSubTask(
            const data::Symbol &topic, const std::shared_ptr<tasks::Callback> &callback)
            : _topic(topic), _callback(callback) {
        }

        std::shared_ptr<data::StructModelBase> runInThread(
            const std::shared_ptr<tasks::Task> &task,
            const std::shared_ptr<data::StructModelBase> &data) override;
    };

    //
    // Handler for a single topic
    //
    class Listener : public data::TrackedObject {
    private:
        data::Symbol _topic;
        std::weak_ptr<Listeners> _parent;
        std::shared_ptr<tasks::Callback> _callback;

    public:
        using BadCastError = errors::InvalidSubscriberError;

        Listener(const Listener &) = delete;
        Listener(Listener &&) noexcept = delete;
        Listener &operator=(const Listener &) = delete;
        Listener &operator=(Listener &&) noexcept = delete;
        ~Listener() override;
        explicit Listener(
            const std::shared_ptr<scope::Context> &context,
            data::Symbol topicOrd,
            Listeners *listeners,
            const std::shared_ptr<tasks::Callback> &callback);
        std::unique_ptr<tasks::SubTask> toSubTask(const data::Symbol &topic);
        std::shared_ptr<data::StructModelBase> runInTaskThread(
            const std::shared_ptr<tasks::Task> &task,
            const std::shared_ptr<data::StructModelBase> &dataIn);
    };

    //
    // All listeners that currently exist
    // No wildcards supported
    //
    class Listeners : public util::RefObject<Listeners> {
    private:
        std::weak_ptr<scope::Context> _context;
        data::Symbol _topic;
        std::vector<std::weak_ptr<Listener>> _listeners;

        scope::Context &context() const {
            return *_context.lock();
        }
        PubSubManager &manager() const {
            return context().lpcTopics();
        }

    protected:
        std::shared_mutex &managerMutex();

    public:
        Listeners(const std::shared_ptr<scope::Context> &context, data::Symbol topic);
        void cleanup();

        bool isEmpty() {
            return _listeners.empty();
        }

        std::shared_ptr<Listener> addNewListener(const std::shared_ptr<tasks::Callback> &callback);
        void fillTopicListeners(std::vector<std::shared_ptr<Listener>> &callOrder);
    };

    //
    // Manages all PubSub behavior
    //
    class PubSubManager {
    private:
        std::weak_ptr<scope::Context> _context;
        scope::SharedContextMapper _symbolMapper;
        data::SymbolValueMap<std::shared_ptr<Listeners>> _topics{_symbolMapper};
        mutable std::shared_mutex _mutex;

        scope::Context &context() const {
            return *_context.lock();
        }

        std::shared_mutex &managerMutex() {
            return _mutex;
        }

    protected:
        friend class Listeners;
        friend class Listener;

    public:
        explicit PubSubManager(const std::shared_ptr<scope::Context> &context)
            : _context(context), _symbolMapper(context) {
        }

        void cleanup();
        // if listeners exist for a given topic, return those listeners
        std::shared_ptr<Listeners> tryGetListeners(data::Symbol topicName);
        // get listeners, create if there is none for given topic
        std::shared_ptr<Listeners> getListeners(data::Symbol topicName);
        // subscribe a new listener to a callback
        std::shared_ptr<Listener> subscribe(
            data::Symbol topic, const std::shared_ptr<tasks::Callback> &callback);
        // subscribe a new listener to a callback with anchoring
        data::ObjectAnchor subscribe(
            data::ObjHandle scopeHandle,
            data::Symbol topic,
            const std::shared_ptr<tasks::Callback> &callback);
        void insertTopicListenerSubTasks(std::shared_ptr<tasks::Task> &task, data::Symbol topic);
        void initializePubSubCall(
            std::shared_ptr<tasks::Task> &task,
            const std::shared_ptr<Listener> &explicitListener,
            data::Symbol topic,
            const std::shared_ptr<data::StructModelBase> &dataIn,
            std::unique_ptr<tasks::SubTask> completion,
            tasks::ExpireTime expireTime);
    };

} // namespace pubsub
