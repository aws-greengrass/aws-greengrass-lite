#pragma once

#include "api_callbacks.hpp"
#include "api_errors.hpp"
#include "api_forwards.hpp"
#include "c_api.hpp"
#include "containers.hpp"
#include "futures.hpp"
#include "handles.hpp"
#include "scopes.hpp"

namespace ggapi {
    class TopicCallback;

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

        explicit Subscription(const SharedHandle &handle) : ObjHandle{handle} {
            check();
        }

        /**
         * Creates a subscription. Note, if the returned object is deleted, the subscription will be
         * deleted, do not ignore return value.
         */
        [[nodiscard]] static Subscription subscribeToTopic(
            Symbol topic, const TopicCallbackLambda &callback);

        /**
         * Creates a subscription. Note, if the returned object is deleted, the subscription will be
         * deleted, do not ignore return value.
         */
        [[nodiscard]] static Subscription subscribeToTopic(
            Symbol topic, const TopicCallback &callback);

        /**
         * Send a message to this specific subscription. Return immediately. If the calling thread
         * is in "single thread" mode, the 'result' callback will not execute until
         * waitForTaskCompleted is called in the same thread.
         */
        [[nodiscard]] Future call(const Container &data) const;

        /**
         * Perform an LPC call to topic.
         * 1/ If there is no handler, a null Promise is returned
         * 2/ If there is one handler, the single Promise is returned
         * 3/ If there is more than one handler, the first handler is returned, others are ignored
         *
         * Synchronous calls perform callTopic(...).waitAndGet();
         */
        [[nodiscard]] static Future callTopicFirst(Symbol topic, const Container &data);

        /**
         * Perform an LPC call to topic. A set of promises is returned
         */
        [[nodiscard]] static FutureSet callTopicAll(Symbol topic, const Container &data);

        [[nodiscard]] static Struct callTopicAndWaitFirst(
            Symbol topic, const Struct &message, int32_t timeout = -1) {
            auto f = callTopicFirst(topic, message);
            if(f) {
                return Struct(f.waitAndGetValue(timeout));
            } else {
                return {};
            }
        }
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
        class TopicDispatch : public CallbackManager::CaptureDispatch<Callable, Args...> {

        public:
            explicit TopicDispatch(Callable callable, Args... args)
                : CallbackManager::CaptureDispatch<Callable, Args...>{
                    std::move(callable), std::move(args)...} {
                static_assert(
                    std::is_invocable_r_v<ObjHandle, Callable, Args..., Symbol, Container>);
            }
            [[nodiscard]] Symbol type() const override {
                return {"topic"};
            }
            [[nodiscard]] CallbackManager::Delegate prepare(
                Symbol callbackType, ggapiDataLen size, void *data) const override {

                auto &cb =
                    this->template checkedStruct<ggapiTopicCallbackData>(callbackType, size, data);
                return this->template prepareWithArgsRet<ObjHandle>(
                    // TODO: Next iteration will remove AnchorHandle
                    // On return, a temporary handle is needed because the handle used may get
                    // released
                    [&cb](const ObjHandle &s) { cb.ret = s.makeTemp(); },
                    Symbol(cb.topicSymbol),
                    ObjHandle::of<Container>(cb.data));
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
        static TopicCallback of(const Callable &callable, const Args &...args) {
            auto dispatch =
                std::make_unique<TopicDispatch<std::decay_t<Callable>, std::decay_t<Args>...>>(
                    callable, args...);
            return CallbackManager::self().registerWithNucleus<TopicCallback>(std::move(dispatch));
        }
    };

    inline Future Subscription::call(const Container &data) const {
        return callHandleApiThrowError<Future>(
            ::ggapiCallDirect, getHandleId(), data.getHandleId());
    }

    inline Future Subscription::callTopicFirst(ggapi::Symbol topic, const Container &data) {
        return callHandleApiThrowError<Future>(
            ::ggapiCallTopicFirst, topic.asInt(), data.getHandleId());
    }

    inline FutureSet Subscription::callTopicAll(ggapi::Symbol topic, const Container &data) {
        auto list =
            callHandleApiThrowError<List>(::ggapiCallTopicAll, topic.asInt(), data.getHandleId());
        auto count = util::safeBoundPositive<int32_t>(list.size());
        std::vector<Future> futures;
        futures.reserve(count);
        for(int32_t i = 0; i < count; ++i) {
            auto f = list.get<Future>(i);
            futures.emplace_back(f);
        }
        return FutureSet(std::move(futures));
    }

    inline Subscription Subscription::subscribeToTopic(
        Symbol topic, const TopicCallback &callback) {

        return callHandleApiThrowError<Subscription>(
            ::ggapiSubscribeToTopic, topic.asInt(), callback.getHandleId());
    }

    inline Subscription Subscription::subscribeToTopic(
        Symbol topic, const TopicCallbackLambda &callback) {
        return subscribeToTopic(topic, TopicCallback::of(callback));
    }

} // namespace ggapi
