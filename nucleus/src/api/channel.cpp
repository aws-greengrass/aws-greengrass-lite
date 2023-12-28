#include "channel/channel.hpp"
#include "data/struct_model.hpp"
#include "errors/error_base.hpp"
#include "scope/context_full.hpp"
#include "tasks/expire_time.hpp"
#include "tasks/task.hpp"
#include "tasks/task_callbacks.hpp"
#include "tasks/task_threads.hpp"
#include <cpp_api.hpp>
#include <cstdint>

bool ggapiIsChannel(uint32_t handle) noexcept {
    return ggapi::trapErrorReturn<bool>([handle]() {
        auto ss{scope::context()->objFromInt(handle)};
        return std::dynamic_pointer_cast<channel::Channel>(ss) != nullptr;
    });
}

uint32_t ggapiCreateChannel() noexcept {
    return ggapi::trapErrorReturn<uint32_t>([]() {
        auto anchor = scope::NucleusCallScopeContext::make<channel::Channel>();
        return anchor.asIntHandle();
    });
}

uint32_t ggapiChannelWrite(uint32_t channel, uint32_t callStruct) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([channel, callStruct]() {
        auto context = scope::context();
        auto channelObj = context->objFromInt<channel::Channel>(channel);
        auto data = context->objFromInt<data::StructModelBase>(callStruct);
        channelObj->write(data);
        return true;
    });
}

uint32_t ggapiChannelClose(uint32_t channel) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([channel]() {
        auto context = scope::context();
        auto channelObj = context->objFromInt<channel::Channel>(channel);
        channelObj->close();
        return true;
    });
}

uint32_t ggapiChannelListen(uint32_t channel, uint32_t callbackHandle) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([channel, callbackHandle]() {
        auto context = scope::context();
        if(!callbackHandle) {
            throw errors::CallbackError("Invalid callback handle");
        }
        auto channelObj = context->objFromInt<channel::Channel>(channel);
        auto callback = context->objFromInt<tasks::Callback>(callbackHandle);
        channelObj->setListenCallback(callback);
        return true;
    });
}

uint32_t ggapiChannelOnClose(uint32_t channel, uint32_t callbackHandle) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([channel, callbackHandle]() {
        auto context = scope::context();
        if(!callbackHandle) {
            throw errors::CallbackError("Invalid callback handle");
        }
        auto channelObj = context->objFromInt<channel::Channel>(channel);
        auto callback = context->objFromInt<tasks::Callback>(callbackHandle);
        channelObj->setCloseCallback(callback);
        return true;
    });
}
