#include "plugins/plugin_loader.hpp"
#include "scope/context_full.hpp"
#include "tasks/task_callbacks.hpp"
#include <cpp_api.hpp>

extern "C" uint32_t ggapiRegisterPlugin(
    uint32_t moduleHandleInt, uint32_t componentNameInt, uint32_t callback) noexcept {
    return ggapi::trapErrorReturn<size_t>([moduleHandleInt, componentNameInt, callback]() {
        auto context = scope::context();
        // Name of new plugin component
        auto componentName = context->symbolFromInt(componentNameInt);
        auto parentModule{context->objFromInt<plugins::AbstractPlugin>(moduleHandleInt)};
        auto lifecycleCallback{context->objFromInt<tasks::Callback>(callback)};
        auto delegate{std::make_shared<plugins::DelegatePlugin>(
            context, componentName.toString(), parentModule, lifecycleCallback)};
        std::shared_ptr<data::TrackingRoot> root;
        if(parentModule) {
            root = parentModule->root();
        } else {
            root = context->pluginLoader().root();
        }
        auto anchor = root->anchor(delegate);
        return anchor.getHandle().asInt();
    });
}

/**
 * Change module context in the call - used by parent modules for delegate modules
 * @return previous handle
 */
extern "C" uint32_t ggapiChangeModule(uint32_t moduleHandleInt) noexcept {
    return ggapi::trapErrorReturn<size_t>([moduleHandleInt]() {
        auto context = scope::context();
        auto targetModule{context->objFromInt<plugins::AbstractPlugin>(moduleHandleInt)};
        auto prev = scope::thread()->setEffectiveModule(targetModule);
        return prev ? prev->getSelf().asInt() : 0;
    });
}
