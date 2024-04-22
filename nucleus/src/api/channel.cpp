#include "channel/channel.hpp"
#include "api_error_trap.hpp"
#include "data/struct_model.hpp"
#include "scope/context_full.hpp"
#include "tasks/task_callbacks.hpp"
#include <cpp_api.hpp>

ggapiErrorKind ggapiIsChannel(ggapiObjHandle handle, ggapiBool *pBool) noexcept {
    return apiImpl::catchErrorToKind([handle, pBool]() {
        auto ss{scope::context()->objFromInt(handle)};
        apiImpl::setBool(pBool, std::dynamic_pointer_cast<channel::Channel>(ss) != nullptr);
    });
}

ggapiErrorKind ggapiCreateChannel(ggapiObjHandle *pHandle) noexcept {
    return apiImpl::catchErrorToKind([pHandle]() {
        auto obj = scope::makeObject<channel::Channel>();
        *pHandle = scope::asIntHandle(obj);
    });
}

uint32_t ggapiChannelWrite(uint32_t channel, uint32_t callStruct) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([channel, callStruct]() {
        auto context = scope::context();
        auto channelObj = context->objFromInt<channel::Channel>(channel);
        auto data = context->objFromInt(callStruct);
        channelObj->write(data);
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
