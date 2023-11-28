#include "plugins/plugin_loader.hpp"
#include "scope/context_full.hpp"
#include "tasks/task_callbacks.hpp"
#include <cpp_api.hpp>

uint32_t ggapiRegisterPlugin(
    uint32_t moduleHandleInt, uint32_t componentNameInt, uint32_t callback) noexcept {
    return ggapi::trapErrorReturn<size_t>([moduleHandleInt, componentNameInt, callback]() {
        scope::Context &context = scope::Context::get();
        // Name of new plugin component
        auto componentName = context.symbolFromInt(componentNameInt);
        auto parentModule{context.objFromInt<plugins::AbstractPlugin>(moduleHandleInt)};
        auto lifecycleCallback{context.objFromInt<tasks::Callback>(callback)};
        auto delegate{std::make_shared<plugins::DelegatePlugin>(
            context.baseRef(), componentName.toString(), parentModule, lifecycleCallback)};
        data::ObjectAnchor anchor = parentModule->root()->anchor(delegate);
        return anchor.getHandle().asInt();
    });
}
