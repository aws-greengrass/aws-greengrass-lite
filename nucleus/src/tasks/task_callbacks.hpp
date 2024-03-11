#pragma once
#include "data/handle_table.hpp"
#include "data/string_table.hpp"
#include <c_api.hpp>
#include <utility>

namespace data {
    class StructModelBase;
}

namespace plugins {
    class AbstractPlugin;
}

namespace tasks {
    class Task;

    class CallbackPackedData {
        data::Symbol _type;

    public:
        explicit CallbackPackedData(const data::Symbol &type) : _type(type) {
        }
        CallbackPackedData(const CallbackPackedData &) = default;
        CallbackPackedData(CallbackPackedData &&) = default;
        CallbackPackedData &operator=(const CallbackPackedData &) = default;
        CallbackPackedData &operator=(CallbackPackedData &&) = default;
        virtual ~CallbackPackedData() = default;
        virtual uint32_t size() const = 0;
        virtual void *data() = 0;
        data::Symbol type() const {
            return _type;
        }
    };

    class TopicCallbackData : public CallbackPackedData {
        ::ggapiTopicCallbackData _packed{};

        static data::Symbol topicType();

    public:
        TopicCallbackData(
            const std::shared_ptr<tasks::Task> &task,
            const data::Symbol &topic,
            const std::shared_ptr<data::StructModelBase> &data);
        uint32_t size() const override;
        void *data() override;
        std::shared_ptr<data::StructModelBase> retVal() const;
    };

    class LifecycleCallbackData : public CallbackPackedData {
        ::ggapiLifecycleCallbackData _packed{};

        static data::Symbol lifecycleType();

    public:
        LifecycleCallbackData(
            const data::ObjHandle &pluginHandle,
            const data::Symbol &phase,
            const data::ObjHandle &dataHandle);
        uint32_t size() const override;
        void *data() override;
        bool retVal() const;
    };

    class TaskCallbackData : public CallbackPackedData {
        ::ggapiTaskCallbackData _packed{};

        static data::Symbol taskType();

    public:
        explicit TaskCallbackData(const std::shared_ptr<data::StructModelBase> &data);
        uint32_t size() const override;
        void *data() override;
    };

    class ChannelListenCallbackData : public CallbackPackedData {
        ::ggapiChannelListenCallbackData _packed{};

        static data::Symbol channelListenCallbackType();

    public:
        explicit ChannelListenCallbackData(const std::shared_ptr<data::StructModelBase> &data);
        uint32_t size() const override;
        void *data() override;
    };

    class ChannelCloseCallbackData : public CallbackPackedData {
        ::ggapiChannelCloseCallbackData _packed{};

        static data::Symbol channelCloseCallbackType();

    public:
        explicit ChannelCloseCallbackData();
        uint32_t size() const override;
        void *data() override;
    };

    class Callback : public data::TrackedObject {

    public:
        explicit Callback(const scope::UsingContext &context) : data::TrackedObject(context) {
        }

        virtual std::shared_ptr<data::StructModelBase> invokeTopicCallback(
            const std::shared_ptr<tasks::Task> &task,
            const data::Symbol &topic,
            const std::shared_ptr<data::StructModelBase> &data);
        virtual bool invokeLifecycleCallback(
            const data::ObjHandle &pluginHandle,
            const data::Symbol &phase,
            const data::ObjHandle &dataHandle);
        virtual void invokeTaskCallback(const std::shared_ptr<data::StructModelBase> &data);
        virtual void invokeChannelListenCallback(
            const std::shared_ptr<data::StructModelBase> &data);
        virtual void invokeChannelCloseCallback();
    };

    class RegisteredCallback : public Callback {
        const data::Symbol _callbackType;
        const std::optional<std::weak_ptr<plugins::AbstractPlugin>> _module;
        const ::ggapiGenericCallback _callback;
        const uintptr_t _callbackCtx;

        [[nodiscard]] std::shared_ptr<plugins::AbstractPlugin> getModule() const;
        static std::optional<std::weak_ptr<plugins::AbstractPlugin>> nullable(
            const std::shared_ptr<plugins::AbstractPlugin> &module) {
            // we need to differentiate between nullptr and expired, so wrap weak_ptr in optional
            if(module) {
                return module;
            } else {
                return {};
            }
        }
        void invoke(CallbackPackedData &packed);
        std::shared_ptr<data::StructModelBase> asStruct(uint32_t retVal);
        static bool asBool(uint32_t retVal);

    public:
        explicit RegisteredCallback(
            const scope::UsingContext &context,
            const std::shared_ptr<plugins::AbstractPlugin> &module,
            data::Symbol callbackType,
            ::ggapiGenericCallback callback,
            uintptr_t callbackCtx) noexcept
            : Callback(context), _callbackType(callbackType), _module(nullable(module)),
              _callback(callback), _callbackCtx(callbackCtx) {
        }
        RegisteredCallback(const RegisteredCallback &) = delete;
        RegisteredCallback(RegisteredCallback &&) = delete;
        RegisteredCallback &operator=(const RegisteredCallback &) = delete;
        RegisteredCallback &operator=(RegisteredCallback &&) = delete;
        ~RegisteredCallback() override;

        std::shared_ptr<data::StructModelBase> invokeTopicCallback(
            const std::shared_ptr<tasks::Task> &task,
            const data::Symbol &topic,
            const std::shared_ptr<data::StructModelBase> &data) override;
        bool invokeLifecycleCallback(
            const data::ObjHandle &pluginHandle,
            const data::Symbol &phase,
            const data::ObjHandle &dataHandle) override;
        void invokeTaskCallback(const std::shared_ptr<data::StructModelBase> &data) override;
        void invokeChannelListenCallback(
            const std::shared_ptr<data::StructModelBase> &data) override;
        void invokeChannelCloseCallback() override;
    };

} // namespace tasks
