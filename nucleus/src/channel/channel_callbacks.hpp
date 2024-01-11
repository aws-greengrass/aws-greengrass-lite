#pragma once
#include "channel.hpp"
#include "tasks/task_callbacks.hpp"

namespace channel {
    template<typename Callable, typename... Args>
    class ChannelListenCallback : public tasks::Callback {
    private:
        const Callable _callable;
        const std::tuple<Args...> _args;

    public:
        explicit ChannelListenCallback(Callable &&callable, Args &&...args)
            : _callable{std::move(callable)}, _args{std::forward<Args>(args)...} {
            static_assert(
                std::is_invocable_v<Callable, Args..., const std::shared_ptr<data::TrackedObject>>);
        }

        void invokeChannelListenCallback(const std::shared_ptr<data::TrackedObject> &obj) override {
            auto args = std::tuple_cat(_args, std::tuple{obj});
            std::apply(_callable, args);
        }
    };

    template<typename Callable, typename... Args>
    class ChannelCloseCallback : public tasks::Callback {
    private:
        const Callable _callable;
        const std::tuple<Args...> _args;

    public:
        explicit ChannelCloseCallback(Callable &&callable, Args &&...args)
            : _callable{std::move(callable)}, _args{std::forward<Args>(args)...} {
            static_assert(std::is_invocable_v<Callable, Args...>);
        }

        void invokeChannelCloseCallback() override {
            std::apply(_callable, _args);
        }
    };

} // namespace channel
