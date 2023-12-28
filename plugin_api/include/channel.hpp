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

        explicit Channel(uint32_t handle) : ObjHandle{handle} {
            check();
        }

        static Channel create() {
            return Channel(::ggapiCreateChannel());
        }

        void write(ObjHandle v) const {
            ::ggapiChannelWrite(_handle, v.getHandleId());
        }

        void close() const {
            ::ggapiChannelClose(_handle);
        }

        inline void addListenCallback(ChannelListenCallback callback);
        template<typename Callback, typename... Args>
        inline void addListenCallback(const Callback callback, Args &&...args);

        inline void addCloseCallback(ChannelCloseCallback callback);
        template<typename Callback, typename... Args>
        inline void addCloseCallback(const Callback callback, Args &&...args);
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
        class ChannelListenDispatch : public CallbackManager::CallbackDispatch {

            const Callable _callable;
            const std::tuple<Args...> _args;

        public:
            explicit ChannelListenDispatch(Callable callable, Args &&...args)
                : _callable{std::move(callable)}, _args{std::forward<Args>(args)...} {
                static_assert(std::is_invocable_v<Callable, Args..., Struct>);
            }
            [[nodiscard]] Symbol type() const override {
                return {"channelListen"};
            }
            [[nodiscard]] CallbackManager::Delegate prepare(
                uint32_t callbackType, uint32_t size, const void *data) const override {
                assertCallbackType(Symbol(callbackType));
                auto &cb = checkedStruct<ChannelListenCallbackData>(size, data);
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
        constexpr ChannelListenCallback() noexcept = default;

        explicit ChannelListenCallback(const ObjHandle &other) : ObjHandle{other} {
        }

        /**
         * Create reference to a channel listen callback.
         */
        template<typename Callable, typename... Args>
        static ChannelListenCallback of(const Callable &callable, Args &&...args) {
            auto dispatch = std::make_unique<ChannelListenDispatch<Callable, Args...>>(
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
        class ChannelCloseDispatch : public CallbackManager::CallbackDispatch {

            const Callable _callable;
            const std::tuple<Args...> _args;

        public:
            explicit ChannelCloseDispatch(Callable callable, Args &&...args)
                : _callable{std::move(callable)}, _args{std::forward<Args>(args)...} {
                static_assert(std::is_invocable_v<Callable, Args...>);
            }
            [[nodiscard]] Symbol type() const override {
                return {"channelClose"};
            }
            [[nodiscard]] CallbackManager::Delegate prepare(
                uint32_t callbackType, uint32_t size, const void *data) const override {
                assertCallbackType(Symbol(callbackType));
                std::ignore = checkedStruct<ChannelCloseCallbackData>(size, data);
                auto callable = _callable;
                auto args = _args;
                return [callable, args]() {
                    std::apply(callable, args);
                    return static_cast<uint32_t>(true);
                };
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
        static ChannelCloseCallback of(const Callable &callable, Args &&...args) {
            auto dispatch = std::make_unique<ChannelCloseDispatch<Callable, Args...>>(
                callable, std::forward<Args>(args)...);
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
    inline void Channel::addListenCallback(const Callback callback, Args &&...args) {
        addListenCallback(ChannelListenCallback::of(callback, std::forward<Args>(args)...));
    }

    inline void Channel::addCloseCallback(ChannelCloseCallback callback) {
        required();
        callApi(
            [*this, callback]() { ::ggapiChannelOnClose(getHandleId(), callback.getHandleId()); });
    }

    template<typename Callback, typename... Args>
    inline void Channel::addCloseCallback(const Callback callback, Args &&...args) {
        addCloseCallback(ChannelCloseCallback::of(callback, std::forward<Args>(args)...));
    }

} // namespace ggapi
