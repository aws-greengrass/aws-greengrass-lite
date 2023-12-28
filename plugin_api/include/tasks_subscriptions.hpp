#pragma once

#include "api_callbacks.hpp"
#include "api_errors.hpp"
#include "api_forwards.hpp"
#include "c_api.hpp"
#include "containers.hpp"
#include "handles.hpp"
#include "scopes.hpp"

namespace ggapi {

    class Struct;
    class Task;
    class TopicCallback;

    //
    // Tasks and Subscriptions are combined as they are inter-related to each other
    //

    /**
     * A task handle represents an active LPC operation or deferred function call. The handle is
     * deleted after the completion callback (if any).
     */
    class Task : public ObjHandle {
        void check() {
            if(getHandleId() != 0 && !isTask()) {
                throw std::runtime_error("Task handle expected");
            }
        }

    public:
        constexpr Task() noexcept = default;

        explicit Task(const ObjHandle &other) : ObjHandle{other} {
            check();
        }

        explicit Task(uint32_t handle) : ObjHandle{handle} {
            check();
        }

        /**
         * Changes affinitized callback model. Listeners created in this thread will only
         * be executed in same thread. Tasks created in this thread will use this thread by
         * default for callbacks if not otherwise affinitized. See individual functions
         * for single thread behavior.
         */
        static void setSingleThread(bool singleThread) {
            callApi([singleThread]() { ::ggapiSetSingleThread(singleThread); });
        }

        /**
         * Create an asynchronous LPC call - returning the Task handle for the call. This
         * function allows a "run later" behavior (e.g. for retries). If calling thread is
         * marked as "single thread", any callbacks not already affinitized will run on this
         * thread during "waitForTaskCompleted".
         */
        [[nodiscard]] static Task sendToTopicAsync(
            Symbol topic,
            Struct message,
            const TopicCallbackLambda &resultCallback,
            int32_t timeout = -1);

        /**
         * Generic form of sendToTopicAsync
         */
        [[nodiscard]] static Task sendToTopicAsync(
            Symbol topic, Struct message, TopicCallback resultCallback, int32_t timeout = -1);

        /**
         * Create a synchronous LPC call - a task handle is created, and observable by subscribers
         * however the task is deleted by the time the call returns. Most handlers are called in
         * the same (callers) thread as if "setSingleThread" set to true, however this must not be
         * assumed as some callbacks may be affinitized to another thread.
         */
        [[nodiscard]] static Struct sendToTopic(Symbol topic, Struct message, int32_t timeout = -1);

        /**
         * A deferred asynchronous call using the task system. If calling thread is in "single
         * thread" mode, the call will not run until "waitForTaskCompleted" is called (for any
         * task).
         */
        [[nodiscard]] static Task callAsync(
            Struct data, const TaskCallbackLambda &callback, uint32_t delay = 0);

        /**
         * Generic form of callAsync
         */
        [[nodiscard]] static Task callAsync(Struct data, TaskCallback callback, uint32_t delay = 0);

        /**
         * Block until task completes including final callback if there is one. If thread is in
         * "single thread" mode, callbacks will execute during this callback even if associated
         * with other tasks.
         */
        [[nodiscard]] Struct waitForTaskCompleted(int32_t timeout = -1);

        /**
         * Block for set period of time while allowing thread to be used for other tasks.
         */
        static void sleep(uint32_t duration);

        /**
         * Cancel task - if a callback is asynchronously executing, they will still continue to
         * run, it does not kill underlying threads.
         */
        void cancelTask();

        /**
         * When in a task callback, returns the associated task. When not in a task callback, it
         * returns a task handle associated with the thread.
         */
        [[nodiscard]] static Task current();
    };

    /**
     * Subscription handles indicate an active listener for LPC topics. Anonymous listeners
     * can also exist. Subscriptions are associated with a scope. A module-scope subscription
     * will exist for the entire lifetime of the module. A local-scope subscription will exist
     * until the enclosing scope returns (useful for single-thread subscriptions).
     */
    class Subscription : public ObjHandle {
        void check() {
            if(getHandleId() != 0 && !isSubscription()) {
                throw std::runtime_error("Subscription handle expected");
            }
        }

    public:
        constexpr Subscription() noexcept = default;

        explicit Subscription(const ObjHandle &other) : ObjHandle{other} {
            check();
        }

        explicit Subscription(uint32_t handle) : ObjHandle{handle} {
            check();
        }

        /**
         * Send a message to this specific subscription. Return immediately. If the calling thread
         * is in "single thread" mode, the 'result' callback will not execute until
         * waitForTaskCompleted is called in the same thread.
         */
        [[nodiscard]] Task callAsync(
            Struct message, const TopicCallbackLambda &resultCallback, int32_t timeout = -1) const;

        /**
         * Generic form of callAsync
         */
        [[nodiscard]] Task callAsync(
            Struct message, TopicCallback result, int32_t timeout = -1) const;

        /**
         * Send a message to this specific subscription. Wait until task completes, as if
         * waitForTaskCompleted is called on the same thread.
         */
        [[nodiscard]] Struct call(Struct message, int32_t timeout = -1) const;
    };

    /**
     * Abstract topic callback
     */
    class TopicCallback : public ObjHandle {

        /**
         * Templated dispatch allows for capturing multiple arguments to pass to a callback
         * routine. Note that this enables the same callback approach as std::invoke and std::thread
         * where a typical use is (callbackMethod, this) but acceptable also to do something like
         * (callbackMethod, this, extraArg1, extraArg2).
         * @tparam Callable Lambda, function pointer, method, etc.
         * @tparam Args Prefix arguments, particularly optional This
         */
        template<typename Callable, typename... Args>
        class TopicDispatch : public CallbackManager::CallbackDispatch {

            const Callable _callable;
            const std::tuple<Args...> _args;

        public:
            explicit TopicDispatch(Callable callable, Args &&...args)
                : _callable{std::move(callable)}, _args{std::forward<Args>(args)...} {
                static_assert(
                    std::is_invocable_r_v<Struct, Callable, Args..., Task, Symbol, Struct>);
            }
            [[nodiscard]] Symbol type() const override {
                return {"topic"};
            }
            [[nodiscard]] CallbackManager::Delegate prepare(
                uint32_t callbackType, uint32_t size, const void *data) const override {
                assertCallbackType(Symbol(callbackType));
                auto &cb = checkedStruct<TopicCallbackData>(size, data);
                auto callable = _callable; // capture copy
                auto task = Task(cb.taskHandle);
                auto topic = Symbol(cb.topicSymbol);
                auto dataStruct = Struct(cb.dataStruct);
                auto args = std::tuple_cat(_args, std::tuple{task, topic, dataStruct});
                return [callable, args]() {
                    Struct s = std::apply(callable, args);
                    return s.getHandleId();
                };
            }
        };

    public:
        constexpr TopicCallback() noexcept = default;

        explicit TopicCallback(const ObjHandle &other) : ObjHandle{other} {
        }

        /**
         * Create reference to a topic callback.
         */
        template<typename Callable, typename... Args>
        static TopicCallback of(const Callable &callable, Args &&...args) {
            auto dispatch = std::make_unique<TopicDispatch<Callable, Args...>>(
                callable, std::forward<Args>(args)...);
            return CallbackManager::self().registerWithNucleus<TopicCallback>(std::move(dispatch));
        }
    };

    /**
     * Abstract task callback
     */
    class TaskCallback : public ObjHandle {

        /**
         * Templated dispatch allows for capturing multiple arguments to pass to a callback
         * routine. Note that this enables the same callback approach as std::invoke and std::thread
         * where a typical use is (callbackMethod, this) but acceptable also to do something like
         * (callbackMethod, this, extraArg1, extraArg2).
         * @tparam Callable Lambda, function pointer, method, etc.
         * @tparam Args Prefix arguments, particularly optional This
         */
        template<typename Callable, typename... Args>
        class TaskDispatch : public CallbackManager::CallbackDispatch {

            const Callable _callable;
            const std::tuple<Args...> _args;

        public:
            explicit TaskDispatch(Callable callable, Args &&...args)
                : _callable{std::move(callable)}, _args{std::forward<Args>(args)...} {
                static_assert(std::is_invocable_v<Callable, Args..., Struct>);
            }
            [[nodiscard]] Symbol type() const override {
                return {"task"};
            }
            [[nodiscard]] CallbackManager::Delegate prepare(
                uint32_t callbackType, uint32_t size, const void *data) const override {
                assertCallbackType(Symbol(callbackType));
                auto &cb = checkedStruct<TaskCallbackData>(size, data);
                auto callable = _callable;
                auto dataStruct = Struct(cb.dataStruct);
                auto args = std::tuple_cat(_args, std::tuple{dataStruct});
                return [callable, args]() {
                    std::apply(callable, args);
                    return static_cast<uint32_t>(true);
                };
            }
        };

    public:
        constexpr TaskCallback() noexcept = default;

        explicit TaskCallback(const ObjHandle &other) : ObjHandle{other} {
        }

        /**
         * Create reference to a simple async task callback.
         */
        template<typename Callable, typename... Args>
        static TaskCallback of(const Callable &callable, Args &&...args) {
            auto dispatch = std::make_unique<TaskDispatch<Callable, Args...>>(
                callable, std::forward<Args>(args)...);
            return CallbackManager::self().registerWithNucleus<TaskCallback>(std::move(dispatch));
        }
    };

    inline Task Task::sendToTopicAsync(
        Symbol topic, Struct message, TopicCallback resultCallback, int32_t timeout) {

        return callApiReturnHandle<Task>([topic, message, resultCallback, timeout]() {
            return ::ggapiSendToTopicAsync(
                topic.asInt(), message.getHandleId(), resultCallback.getHandleId(), timeout);
        });
    }

    inline Task Task::sendToTopicAsync(
        Symbol topic, Struct message, const TopicCallbackLambda &resultCallback, int32_t timeout) {

        return sendToTopicAsync(topic, message, TopicCallback::of(resultCallback), timeout);
    }

    inline Struct Task::sendToTopic(ggapi::Symbol topic, Struct message, int32_t timeout) {
        return callApiReturnHandle<Struct>([topic, message, timeout]() {
            return ::ggapiSendToTopic(topic.asInt(), message.getHandleId(), timeout);
        });
    }

    inline Struct Task::waitForTaskCompleted(int32_t timeout) {
        required();
        return callApiReturnHandle<Struct>(
            [this, timeout]() { return ::ggapiWaitForTaskCompleted(getHandleId(), timeout); });
    }

    inline void Task::sleep(uint32_t duration) {
        return callApi([duration]() { ::ggapiSleep(duration); });
    }

    inline void Task::cancelTask() {
        required();
        callApi([this]() { return ::ggapiCancelTask(getHandleId()); });
    }

    inline Task Task::current() {
        return callApiReturnHandle<Task>([]() { return ::ggapiGetCurrentTask(); });
    }

    inline Task Task::callAsync(Struct data, TaskCallback callback, uint32_t delay) {
        return callApiReturnHandle<Task>([data, callback, delay]() {
            return ::ggapiCallAsync(data.getHandleId(), callback.getHandleId(), delay);
        });
    }

    inline Task Task::callAsync(Struct data, const TaskCallbackLambda &callback, uint32_t delay) {
        return callAsync(data, TaskCallback::of(callback), delay);
    }

    inline Task Subscription::callAsync(
        Struct message, TopicCallback resultCallback, int32_t timeout) const {
        required();
        return callApiReturnHandle<Task>([this, message, resultCallback, timeout]() {
            return ::ggapiSendToListenerAsync(
                getHandleId(), message.getHandleId(), resultCallback.getHandleId(), timeout);
        });
    }

    inline Task Subscription::callAsync(
        Struct message, const TopicCallbackLambda &resultCallback, int32_t timeout) const {
        required();
        return callAsync(message, TopicCallback::of(resultCallback), timeout);
    }

    inline Struct Subscription::call(Struct message, int32_t timeout) const {
        required();
        return callApiReturnHandle<Struct>([this, message, timeout]() {
            return ::ggapiSendToListener(getHandleId(), message.getHandleId(), timeout);
        });
    }

    //
    // These inline have cross-dependencies that require multiple includes to resolve
    //

    inline Subscription Scope::subscribeToTopic(Symbol topic, TopicCallback callback) {
        required();
        return callApiReturnHandle<Subscription>([*this, topic, callback]() {
            return ::ggapiSubscribeToTopic(getHandleId(), topic.asInt(), callback.getHandleId());
        });
    }

    inline Subscription Scope::subscribeToTopic(Symbol topic, const TopicCallbackLambda &callback) {
        return subscribeToTopic(topic, TopicCallback::of(callback));
    }

    inline ModuleScope ModuleScope::registerPlugin(
        Symbol componentName, LifecycleCallback callback) {
        required();
        return callApiReturnHandle<ModuleScope>([*this, componentName, callback]() {
            return ::ggapiRegisterPlugin(
                getHandleId(), componentName.asInt(), callback.getHandleId());
        });
    }

    inline ModuleScope ModuleScope::registerPlugin(
        Symbol componentName, const LifecycleCallbackLambda &callback) {
        return registerPlugin(componentName, LifecycleCallback::of(callback));
    }

    inline ModuleScope ModuleScope::registerGlobalPlugin(
        Symbol componentName, LifecycleCallback callback) {
        return callApiReturnHandle<ModuleScope>([componentName, callback]() {
            return ::ggapiRegisterPlugin(0, componentName.asInt(), callback.getHandleId());
        });
    }

    inline ModuleScope ModuleScope::registerGlobalPlugin(
        Symbol componentName, const LifecycleCallbackLambda &callback) {
        return registerGlobalPlugin(componentName, LifecycleCallback::of(callback));
    }

} // namespace ggapi
