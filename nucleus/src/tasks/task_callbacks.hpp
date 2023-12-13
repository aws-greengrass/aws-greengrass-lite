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
        virtual const void *data() const = 0;
        data::Symbol type() const {
            return _type;
        }
    };

    class TopicCallbackData : public CallbackPackedData {
        ::TopicCallbackData _packed{};

        static data::Symbol topicType();

    public:
        TopicCallbackData(
            const std::shared_ptr<tasks::Task> &task,
            const data::Symbol &topic,
            const std::shared_ptr<data::StructModelBase> &data);
        uint32_t size() const override;
        const void *data() const override;
    };

    class LifecycleCallbackData : public CallbackPackedData {
        ::LifecycleCallbackData _packed{};

        static data::Symbol lifecycleType();

    public:
        LifecycleCallbackData(
            const data::ObjHandle &pluginHandle,
            const data::Symbol &phase,
            const data::ObjHandle &dataHandle);
        uint32_t size() const override;
        const void *data() const override;
    };

    class TaskCallbackData : public CallbackPackedData {
        ::TaskCallbackData _packed{};

        static data::Symbol taskType();

    public:
        explicit TaskCallbackData(const std::shared_ptr<data::StructModelBase> &data);
        uint32_t size() const override;
        const void *data() const override;
    };

    class Callback : public data::TrackedObject {

    public:
        explicit Callback(const std::shared_ptr<scope::Context> &context)
            : data::TrackedObject(context) {
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
        uint32_t invoke(const CallbackPackedData &packed);
        std::shared_ptr<data::StructModelBase> asStruct(uint32_t retVal);
        static bool asBool(uint32_t retVal);

    public:
        explicit RegisteredCallback(
            const std::shared_ptr<scope::Context> &context,
            const std::shared_ptr<plugins::AbstractPlugin> &module,
            data::Symbol callbackType,
            ::ggapiGenericCallback callback,
            uintptr_t callbackCtx) noexcept
            : Callback(context), _module(nullable(module)), _callbackType(callbackType),
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
    };

} // namespace tasks
