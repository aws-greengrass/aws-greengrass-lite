#include "task_callbacks.hpp"
#include "data/struct_model.hpp"
#include "plugins/plugin_loader.hpp"
#include "scope/context_full.hpp"
#include "tasks/task.hpp"

namespace tasks {

    uint32_t RegisteredCallback::invoke(const CallbackPackedData &packed) {

        // No mutex required as the member variables are immutable
        // Assume a scope was allocated prior to this call

        errors::ThreadErrorContainer::get().clear();
        auto resIntHandle =
            _callback(_callbackCtx, packed.type().asInt(), packed.size(), packed.data());
        if(resIntHandle == 0) {
            errors::ThreadErrorContainer::get().throwIfError();
        }
        return resIntHandle;
    }

    bool RegisteredCallback::asBool(uint32_t retVal) {
        return retVal != 0;
    }

    std::shared_ptr<data::StructModelBase> RegisteredCallback::asStruct(uint32_t retVal) {
        return context().objFromInt<data::StructModelBase>(retVal);
    }

    RegisteredCallback::~RegisteredCallback() {
        scope::StackScope scope{};
        errors::ThreadErrorContainer::get().clear();
        // Signal callback has been released
        std::ignore = _callback(_callbackCtx, 0, 0, nullptr);
    }

    data::Symbol TopicCallbackData::topicType() {
        static data::Symbol topic = scope::context().intern("topic");
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

    const void *TopicCallbackData::data() const {
        return &_packed;
    }

    data::Symbol LifecycleCallbackData::lifecycleType() {
        static data::Symbol topic = scope::context().intern("lifecycle");
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

    const void *LifecycleCallbackData::data() const {
        return &_packed;
    }

    data::Symbol TaskCallbackData::taskType() {
        static data::Symbol task = scope::context().intern("task");
        return task;
    }

    TaskCallbackData::TaskCallbackData(const std::shared_ptr<data::StructModelBase> &data)
        : CallbackPackedData(taskType()) {

        _packed.dataStruct = scope::NucleusCallScopeContext::intHandle(data);
    }

    uint32_t TaskCallbackData::size() const {
        return sizeof(_packed);
    }

    const void *TaskCallbackData::data() const {
        return &_packed;
    }

    std::shared_ptr<data::StructModelBase> RegisteredCallback::invokeTopicCallback(
        const std::shared_ptr<tasks::Task> &task,
        const data::Symbol &topic,
        const std::shared_ptr<data::StructModelBase> &data) {

        if(_callbackType != context().intern("topic")) {
            throw std::runtime_error("Mismatched callback");
        }

        scope::StackScope scope{};
        tasks::TopicCallbackData packed{task, topic, data};
        return asStruct(invoke(packed));
    }

    bool RegisteredCallback::invokeLifecycleCallback(
        const data::ObjHandle &pluginHandle,
        const data::Symbol &phase,
        const data::ObjHandle &dataHandle) {

        if(_callbackType != context().intern("lifecycle")) {
            throw std::runtime_error("Mismatched callback");
        }

        scope::StackScope scope{};
        tasks::LifecycleCallbackData packed{pluginHandle, phase, dataHandle};
        return asBool(invoke(packed));
    }
    void RegisteredCallback::invokeTaskCallback(
        const std::shared_ptr<data::StructModelBase> &data) {

        if(_callbackType != context().intern("task")) {
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
} // namespace tasks
