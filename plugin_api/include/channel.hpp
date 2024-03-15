#pragma once

#include "api_callbacks.hpp"
#include "api_forwards.hpp"
#include "c_api.hpp"
#include "containers.hpp"
#include "handles.hpp"

namespace ggapi {

    /**
     * Channels are streams of data.
     */
    class Channel : public ObjHandle {
        void check() {
            if(getHandleId() != 0 && !isChannel()) {
                throw std::runtime_error("Subscription handle expected");
            }
        }

    public:
        constexpr Channel() noexcept = default;

        explicit Channel(const ObjHandle &other) : ObjHandle{other} {
            check();
        }

        explicit Channel(const SharedHandle &handle) : ObjHandle{handle} {
            check();
        }

        static Channel create() {
            return callHandleApiThrowError<Channel>(::ggapiCreateChannel);
        }

        void write(ObjHandle v) const {
            ::ggapiChannelWrite(asId(), v.getHandleId());
        }

        inline void addListenCallback(ChannelListenCallback callback);
        template<typename Callback, typename... Args>
        inline void addListenCallback(const Callback &callback, const Args &...args);

        inline void addCloseCallback(ChannelCloseCallback callback);
        template<typename Callback, typename... Args>
        inline void addCloseCallback(const Callback &callback, const Args &...args);
    };

    /**
     * Abstract channel listen callback
     */
    class ChannelListenCallback : public ObjHandle {

        /**
         * Templated dispatch allows for capturing multiple arguments to pass to a callback
         * routine. Note that this enables the same callback approach as std::invoke and std::thread
         * where a typical use is (callbackMethod, this) but acceptable also to do something like
         * (callbackMethod, this, extraArg1, extraArg2).
         * @tparam Callable Lambda, function pointer, method, etc.
         * @tparam Args Prefix arguments, particularly optional This
         */
        template<typename Callable, typename... Args>
        class ChannelListenDispatch : public CallbackManager::CaptureDispatch<Callable, Args...> {

        public:
            explicit ChannelListenDispatch(Callable callable, Args... args)
                : CallbackManager::CaptureDispatch<Callable, Args...>{
                    std::move(callable), std::move(args)...} {
                static_assert(std::is_invocable_v<Callable, Args..., Struct>);
            }
            [[nodiscard]] Symbol type() const override {
                return {"channelListen"};
            }
            [[nodiscard]] CallbackManager::Delegate prepare(
                Symbol callbackType, ggapiDataLen size, void *data) const override {

                auto &cb = this->template checkedStruct<ggapiChannelListenCallbackData>(
                    callbackType, size, data);
                // TODO: change to container
                return this->prepareWithArgs(ggapi::ObjHandle::of<Struct>(cb.data));
            }
        };

    public:
        constexpr ChannelListenCallback() noexcept = default;

        explicit ChannelListenCallback(const ObjHandle &other) : ObjHandle{other} {
        }

        /**
         * Create reference to a channel listen callback.
         */
        template<typename Callable, typename... Args>
        static ChannelListenCallback of(const Callable &callable, Args &...args) {
            auto dispatch = std::make_unique<
                ChannelListenDispatch<std::decay_t<Callable>, std::decay_t<Args>...>>(
                callable, std::forward<Args>(args)...);
            return CallbackManager::self().registerWithNucleus<ChannelListenCallback>(
                std::move(dispatch));
        }
    };

    /**
     * Abstract channel close callback
     */
    class ChannelCloseCallback : public ObjHandle {

        /**
         * Templated dispatch allows for capturing multiple arguments to pass to a callback
         * routine. Note that this enables the same callback approach as std::invoke and std::thread
         * where a typical use is (callbackMethod, this) but acceptable also to do something like
         * (callbackMethod, this, extraArg1, extraArg2).
         * @tparam Callable Lambda, function pointer, method, etc.
         * @tparam Args Prefix arguments, particularly optional This
         */
        template<typename Callable, typename... Args>
        class ChannelCloseDispatch : public CallbackManager::CaptureDispatch<Callable, Args...> {

        public:
            explicit ChannelCloseDispatch(Callable callable, Args... args)
                : CallbackManager::CaptureDispatch<Callable, Args...>{
                    std::move(callable), std::move(args)...} {
                static_assert(std::is_invocable_v<Callable, Args...>);
            }
            [[nodiscard]] Symbol type() const override {
                return {"channelClose"};
            }
            [[nodiscard]] CallbackManager::Delegate prepare(
                Symbol callbackType, ggapiDataLen size, void *data) const override {

                std::ignore = this->template checkedStruct<ggapiChannelListenCallbackData>(
                    callbackType, size, data);
                return this->prepareWithArgs();
            }
        };

    public:
        constexpr ChannelCloseCallback() noexcept = default;

        explicit ChannelCloseCallback(const ObjHandle &other) : ObjHandle{other} {
        }

        /**
         * Create reference to a channel close callback.
         */
        template<typename Callable, typename... Args>
        static ChannelCloseCallback of(const Callable &callable, const Args &...args) {
            auto dispatch = std::make_unique<
                ChannelCloseDispatch<std::decay_t<Callable>, std::decay_t<Args>...>>(
                callable, args...);
            return CallbackManager::self().registerWithNucleus<ChannelCloseCallback>(
                std::move(dispatch));
        }
    };

    inline void Channel::addListenCallback(ChannelListenCallback callback) {
        required();
        callApi(
            [*this, callback]() { ::ggapiChannelListen(getHandleId(), callback.getHandleId()); });
    }

    template<typename Callback, typename... Args>
    inline void Channel::addListenCallback(const Callback &callback, const Args &...args) {
        addListenCallback(ChannelListenCallback::of(callback, args...));
    }

    inline void Channel::addCloseCallback(ChannelCloseCallback callback) {
        required();
        callApi(
            [*this, callback]() { ::ggapiChannelOnClose(getHandleId(), callback.getHandleId()); });
    }

    template<typename Callback, typename... Args>
    inline void Channel::addCloseCallback(const Callback &callback, const Args &...args) {
        addCloseCallback(ChannelCloseCallback::of(callback, args...));
    }

} // namespace ggapi
