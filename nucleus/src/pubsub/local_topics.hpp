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
} // namespace tasks

namespace data {
    class StructModelBase;
}

namespace pubsub {
    class Future;
    class Listeners;
    class PubSubManager;

    //
    // Handler for a single topic
    //
    class Listener : public data::TrackedObject {
    private:
        const data::Symbol _topic;
        const std::weak_ptr<Listeners> _parent;
        const std::shared_ptr<tasks::Callback> _callback;

    public:
        using BadCastError = errors::InvalidSubscriberError;

        Listener(const Listener &) = delete;
        Listener(Listener &&) noexcept = delete;
        Listener &operator=(const Listener &) = delete;
        Listener &operator=(Listener &&) noexcept = delete;
        ~Listener() noexcept override;
        explicit Listener(
            const scope::UsingContext &context,
            data::Symbol topicOrd,
            const std::shared_ptr<Listeners> &listeners,
            const std::shared_ptr<tasks::Callback> &callback);
        std::shared_ptr<pubsub::Future> call(
            const std::shared_ptr<data::ContainerModelBase> &dataIn);
        void close() override;
        void closeImpl() noexcept;
    };

    //
    // All listeners that currently exist
    // No wildcards supported
    //
    class Listeners : public util::RefObject<Listeners>, protected scope::UsesContext {
        friend class Listener;
        friend class PubSubManager;

    private:
        const data::Symbol _topic;
        std::vector<std::weak_ptr<Listener>> _listeners; // Protected by mutex
        PubSubManager &manager() const;

    protected:
        std::shared_mutex &managerMutex();

        bool isEmptyMutexHeld() {
            return _listeners.empty();
        }

    public:
        Listeners(const scope::UsingContext &context, data::Symbol topic);
        void cleanup();

        std::shared_ptr<Listener> addNewListener(const std::shared_ptr<tasks::Callback> &callback);
        void fillTopicListeners(std::vector<std::shared_ptr<Listener>> &callOrder);
    };

    //
    // Manages all PubSub behavior
    //
    class PubSubManager : private scope::UsesContext {
    private:
        data::SymbolValueMap<std::shared_ptr<Listeners>> _topics{context()};
        mutable std::shared_mutex _mutex;

        std::shared_mutex &managerMutex() {
            return _mutex;
        }

    protected:
        friend class Listeners;
        friend class Listener;

    public:
        using scope::UsesContext::UsesContext;

        void cleanup();
        // if listeners exist for a given topic, return those listeners
        std::shared_ptr<Listeners> tryGetListeners(data::Symbol topicName);
        // get listeners, create if there is none for given topic
        std::shared_ptr<Listeners> getListeners(data::Symbol topicName);
        // subscribe a new listener to a callback
        std::shared_ptr<Listener> subscribe(
            data::Symbol topic, const std::shared_ptr<tasks::Callback> &callback);
        std::shared_ptr<Future> callFirst(
            data::Symbol topic, const std::shared_ptr<data::ContainerModelBase> &dataIn);
        std::vector<std::shared_ptr<Future>> callAll(
            data::Symbol topic, const std::shared_ptr<data::ContainerModelBase> &dataIn);
    };

} // namespace pubsub
