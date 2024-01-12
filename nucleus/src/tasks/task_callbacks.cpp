#include "task_callbacks.hpp"
#include "data/struct_model.hpp"
#include "errors/errors.hpp"
#include "plugins/plugin_loader.hpp"
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
        scope::StackScope scope{};
        errors::ThreadErrorContainer::get().clear();
        // Signal callback has been released
        std::ignore = _callback(_callbackCtx, 0, 0, nullptr);
    }

    data::Symbol TopicCallbackData::topicType() {
        static data::Symbol topic = scope::context()->intern("topic");
        return topic;
    }

    TopicCallbackData::TopicCallbackData(
        const std::shared_ptr<tasks::Task> &task,
        const data::Symbol &topic,
        const std::shared_ptr<data::StructModelBase> &data)
        : CallbackPackedData(topicType()) {

        _packed.taskHandle = scope::NucleusCallScopeContext::intHandle(task);
        _packed.topicSymbol = topic.asInt();
        _packed.dataStruct = scope::NucleusCallScopeContext::intHandle(data);
    }

    uint32_t TopicCallbackData::size() const {
        return sizeof(_packed);
    }

    void *TopicCallbackData::data() {
        return &_packed;
    }

    std::shared_ptr<data::StructModelBase> TopicCallbackData::retVal() const {
        if(_packed.retDataStruct != 0) {
            return scope::context()->objFromInt<data::StructModelBase>(_packed.retDataStruct);
        } else {
            return {};
        }
    }

    data::Symbol LifecycleCallbackData::lifecycleType() {
        static data::Symbol topic = scope::context()->intern("lifecycle");
        return topic;
    }

    LifecycleCallbackData::LifecycleCallbackData(
        const data::ObjHandle &pluginHandle,
        const data::Symbol &phase,
        const data::ObjHandle &dataHandle)
        : CallbackPackedData(lifecycleType()) {

        _packed.moduleHandle = pluginHandle.asInt();
        _packed.phaseSymbol = phase.asInt();
        _packed.dataStruct = dataHandle.asInt();
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

    data::Symbol TaskCallbackData::taskType() {
        static data::Symbol task = scope::context()->intern("task");
        return task;
    }

    TaskCallbackData::TaskCallbackData(const std::shared_ptr<data::StructModelBase> &data)
        : CallbackPackedData(taskType()) {

        _packed.dataStruct = scope::NucleusCallScopeContext::intHandle(data);
    }

    uint32_t TaskCallbackData::size() const {
        return sizeof(_packed);
    }

    void *TaskCallbackData::data() {
        return &_packed;
    }

    data::Symbol ChannelListenCallbackData::channelListenCallbackType() {
        static data::Symbol task = scope::context()->intern("channelListen");
        return task;
    }

    ChannelListenCallbackData::ChannelListenCallbackData(
        const std::shared_ptr<data::StructModelBase> &data)
        : CallbackPackedData(channelListenCallbackType()) {

        _packed.dataStruct = scope::NucleusCallScopeContext::intHandle(data);
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
    }

    uint32_t ChannelCloseCallbackData::size() const {
        return sizeof(_packed);
    }

    void *ChannelCloseCallbackData::data() {
        return &_packed;
    }

    std::shared_ptr<data::StructModelBase> RegisteredCallback::invokeTopicCallback(
        const std::shared_ptr<tasks::Task> &task,
        const data::Symbol &topic,
        const std::shared_ptr<data::StructModelBase> &data) {

        if(_callbackType != context()->intern("topic")) {
            throw std::runtime_error("Mismatched callback");
        }

        scope::StackScope scope{};
        tasks::TopicCallbackData packed{task, topic, data};
        invoke(packed);
        return packed.retVal();
    }

    bool RegisteredCallback::invokeLifecycleCallback(
        const data::ObjHandle &pluginHandle,
        const data::Symbol &phase,
        const data::ObjHandle &dataHandle) {

        if(_callbackType != context()->intern("lifecycle")) {
            throw std::runtime_error("Mismatched callback");
        }

        scope::StackScope scope{};
        tasks::LifecycleCallbackData packed{pluginHandle, phase, dataHandle};
        invoke(packed);
        return packed.retVal();
    }
    void RegisteredCallback::invokeTaskCallback(
        const std::shared_ptr<data::StructModelBase> &data) {

        if(_callbackType != context()->intern("task")) {
            throw std::runtime_error("Mismatched callback");
        }

        scope::StackScope scope{};
        tasks::TaskCallbackData packed{data};
        invoke(packed);
    }
    std::shared_ptr<data::StructModelBase> Callback::invokeTopicCallback(
        const std::shared_ptr<tasks::Task> &task,
        const data::Symbol &topic,
        const std::shared_ptr<data::StructModelBase> &data) {
        throw std::runtime_error("Mismatched callback");
    }
    bool Callback::invokeLifecycleCallback(
        const data::ObjHandle &pluginHandle,
        const data::Symbol &phase,
        const data::ObjHandle &dataHandle) {
        throw std::runtime_error("Mismatched callback");
    }
    void Callback::invokeTaskCallback(const std::shared_ptr<data::StructModelBase> &data) {
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

        scope::StackScope scope{};
        tasks::ChannelListenCallbackData packed{data};
        invoke(packed);
    }
    void RegisteredCallback::invokeChannelCloseCallback() {

        if(_callbackType != context()->intern("channelClose")) {
            throw std::runtime_error("Mismatched callback");
        }

        scope::StackScope scope{};
        tasks::ChannelCloseCallbackData packed{};
        invoke(packed);
    }
} // namespace tasks
