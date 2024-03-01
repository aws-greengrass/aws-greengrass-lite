#include "task_callbacks.hpp"
#include "data/struct_model.hpp"
#include "errors/errors.hpp"
#include "plugins/plugin_loader.hpp"
#include "pubsub/promise.hpp"
#include "scope/context_full.hpp"
#include "tasks/task.hpp"

const auto LOG = // NOLINT(cert-err58-cpp)
    logging::Logger::of("com.aws.greengrass.tasks.RegisteredCallback");

namespace tasks {

    std::shared_ptr<plugins::AbstractPlugin> RegisteredCallback::getModule() const {
        if(_module.has_value()) {
            std::shared_ptr<plugins::AbstractPlugin> module{_module.value().lock()};
            if(!module) {
                throw errors::CallbackError("Target module unloaded");
            }
            return module;
        } else {
            return {};
        }
    }

    void RegisteredCallback::invoke(CallbackPackedData &packed) {

        auto module = getModule();
        plugins::CurrentModuleScope moduleScope(module);

        // No mutex required as the member variables are immutable
        // Assume a scope was allocated prior to this call

        errors::ThreadErrorContainer::get().clear();
        auto errorKind =
            _callback(_callbackCtx, packed.type().asInt(), packed.size(), packed.data());
        errors::Error::throwThreadError(errorKind);
    }

    RegisteredCallback::~RegisteredCallback() {
        errors::ThreadErrorContainer::get().clear();
        // Signal callback has been released
        std::ignore = _callback(_callbackCtx, 0, 0, nullptr);
    }

    data::Symbol TopicCallbackData::topicType() {
        static data::Symbol topic = scope::context()->intern("topic");
        return topic;
    }

    TopicCallbackData::TopicCallbackData(
        const data::Symbol &topic, const std::shared_ptr<data::ContainerModelBase> &data)
        : CallbackPackedData(topicType()) {

        _packed.topicSymbol = topic.asInt();
        _packed.data = scope::asIntHandle(data);
        _packed.ret = 0;
    }

    uint32_t TopicCallbackData::size() const {
        return sizeof(_packed);
    }

    void *TopicCallbackData::data() {
        return &_packed;
    }

    std::shared_ptr<pubsub::Future> TopicCallbackData::retVal() const {
        if(_packed.ret != 0) {
            auto obj = scope::context()->objFromInt<data::TrackedObject>(_packed.ret);
            auto asFuture = std::dynamic_pointer_cast<pubsub::FutureBase>(obj);
            if(asFuture != nullptr) {
                return asFuture->getFuture();
            }
            try {
                auto asContainer = obj->ref<data::ContainerModelBase>();
                auto promise = std::make_shared<pubsub::Promise>(scope::Context::get());
                promise->setValue(asContainer);
                return promise->getFuture();
            } catch(std::bad_cast &) {
                throw data::ContainerModelBase::BadCastError();
            }
        } else {
            return {};
        }
    }

    data::Symbol AsyncCallbackData::asyncType() {
        static data::Symbol async = scope::context()->intern("async");
        return async;
    }

    AsyncCallbackData::AsyncCallbackData() : CallbackPackedData(asyncType()) {
        _packed._dummy = 0;
    }

    uint32_t AsyncCallbackData::size() const {
        return sizeof(_packed);
    }

    void *AsyncCallbackData::data() {
        return &_packed;
    }

    data::Symbol FutureCallbackData::futureType() {
        static data::Symbol future = scope::context()->intern("future");
        return future;
    }

    FutureCallbackData::FutureCallbackData(const std::shared_ptr<pubsub::Future> &future)
        : CallbackPackedData(futureType()) {

        _packed.futureHandle = scope::asIntHandle(future);
    }

    uint32_t FutureCallbackData::size() const {
        return sizeof(_packed);
    }

    void *FutureCallbackData::data() {
        return &_packed;
    }

    data::Symbol LifecycleCallbackData::lifecycleType() {
        static data::Symbol lifecycle = scope::context()->intern("lifecycle");
        return lifecycle;
    }

    LifecycleCallbackData::LifecycleCallbackData(
        const std::shared_ptr<plugins::AbstractPlugin> &module,
        const data::Symbol &phase,
        const std::shared_ptr<data::ContainerModelBase> &data)
        : CallbackPackedData(lifecycleType()) {

        _packed.moduleHandle = scope::asIntHandle(module);
        _packed.phaseSymbol = phase.asInt();
        _packed.dataStruct = scope::asIntHandle(data);
        _packed.retWasHandled = 0;
    }

    uint32_t LifecycleCallbackData::size() const {
        return sizeof(_packed);
    }

    void *LifecycleCallbackData::data() {
        return &_packed;
    }

    bool LifecycleCallbackData::retVal() const {
        return _packed.retWasHandled != 0; // Normalize true value
    }

    data::Symbol ChannelListenCallbackData::channelListenCallbackType() {
        static data::Symbol task = scope::context()->intern("channelListen");
        return task;
    }

    ChannelListenCallbackData::ChannelListenCallbackData(
        const std::shared_ptr<data::StructModelBase> &data)
        : CallbackPackedData(channelListenCallbackType()) {

        _packed.dataStruct = scope::asIntHandle(data);
    }

    uint32_t ChannelListenCallbackData::size() const {
        return sizeof(_packed);
    }

    void *ChannelListenCallbackData::data() {
        return &_packed;
    }

    data::Symbol ChannelCloseCallbackData::channelCloseCallbackType() {
        static data::Symbol task = scope::context()->intern("channelClose");
        return task;
    }

    ChannelCloseCallbackData::ChannelCloseCallbackData()
        : CallbackPackedData(channelCloseCallbackType()) {
        _packed._dummy = 0;
    }

    uint32_t ChannelCloseCallbackData::size() const {
        return sizeof(_packed);
    }

    void *ChannelCloseCallbackData::data() {
        return &_packed;
    }

    std::shared_ptr<pubsub::Future> RegisteredCallback::invokeTopicCallback(
        const data::Symbol &topic, const std::shared_ptr<data::ContainerModelBase> &data) {

        if(_callbackType != context()->intern("topic")) {
            throw std::runtime_error("Mismatched callback");
        }

        scope::TempRoot tempRoot;
        tasks::TopicCallbackData packed{topic, data};
        invoke(packed);
        return packed.retVal();
    }

    void RegisteredCallback::invokeAsyncCallback() {

        if(_callbackType != context()->intern("async")) {
            throw std::runtime_error("Mismatched callback");
        }

        scope::TempRoot tempRoot;
        tasks::AsyncCallbackData packed{};
        invoke(packed);
    }

    void RegisteredCallback::invokeFutureCallback(const std::shared_ptr<pubsub::Future> &future) {

        if(_callbackType != context()->intern("future")) {
            throw std::runtime_error("Mismatched callback");
        }

        scope::TempRoot tempRoot;
        tasks::FutureCallbackData packed{future};
        invoke(packed);
    }

    bool RegisteredCallback::invokeLifecycleCallback(
        const std::shared_ptr<plugins::AbstractPlugin> &module,
        const data::Symbol &phase,
        const std::shared_ptr<data::ContainerModelBase> &data) {

        if(_callbackType != context()->intern("lifecycle")) {
            throw std::runtime_error("Mismatched callback");
        }

        scope::TempRoot tempRoot;
        tasks::LifecycleCallbackData packed{module, phase, data};
        invoke(packed);
        return packed.retVal();
    }

    std::shared_ptr<pubsub::Future> Callback::invokeTopicCallback(
        const data::Symbol &topic, const std::shared_ptr<data::ContainerModelBase> &data) {
        throw std::runtime_error("Mismatched callback");
    }

    void Callback::invokeAsyncCallback() {
        throw std::runtime_error("Mismatched callback");
    }

    void Callback::invokeFutureCallback(const std::shared_ptr<pubsub::Future> &future) {
        throw std::runtime_error("Mismatched callback");
    }

    bool Callback::invokeLifecycleCallback(
        const std::shared_ptr<plugins::AbstractPlugin> &module,
        const data::Symbol &phase,
        const std::shared_ptr<data::ContainerModelBase> &data) {
        throw std::runtime_error("Mismatched callback");
    }

    void Callback::invokeChannelListenCallback(const std::shared_ptr<data::StructModelBase> &data) {
        throw std::runtime_error("Mismatched callback");
    }
    void Callback::invokeChannelCloseCallback() {
        throw std::runtime_error("Mismatched callback");
    }
    void RegisteredCallback::invokeChannelListenCallback(
        const std::shared_ptr<data::StructModelBase> &data) {

        if(_callbackType != context()->intern("channelListen")) {
            throw std::runtime_error("Mismatched callback");
        }

        scope::TempRoot tempRoot;
        tasks::ChannelListenCallbackData packed{data};
        invoke(packed);
    }
    void RegisteredCallback::invokeChannelCloseCallback() {

        if(_callbackType != context()->intern("channelClose")) {
            throw std::runtime_error("Mismatched callback");
        }

        scope::TempRoot tempRoot;
        tasks::ChannelCloseCallbackData packed{};
        invoke(packed);
    }

    void AsyncCallbackTask::invoke() {
        _callback->invokeAsyncCallback();
    }

} // namespace tasks
