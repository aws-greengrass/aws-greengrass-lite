#include "scope/context_full.hpp"
#include <cpp_api.hpp>

uint32_t ggapiRegisterPlugin(
    uint32_t moduleHandleInt,
    uint32_t componentNameInt,
    ggapiLifecycleCallback lifecycleCallback,
    uintptr_t callbackContext) noexcept {
    return ggapi::trapErrorReturn<size_t>(
        [moduleHandleInt, componentNameInt, lifecycleCallback, callbackContext]() {
            scope::Context &context = scope::Context::get();
            // Handle of owning module
            auto moduleHandle = context.handleFromInt(moduleHandleInt);
            // Name of new plugin component
            auto componentName = context.symbolFromInt(componentNameInt);
            auto parentModule{moduleHandle.toObject<plugins::AbstractPlugin>()};
            auto delegate{std::make_shared<plugins::DelegatePlugin>(
                context.baseRef(),
                componentName.toString(),
                parentModule,
                lifecycleCallback,
                callbackContext)};
            data::ObjectAnchor anchor = parentModule->root()->anchor(delegate);
            return anchor.getHandle().asInt();
        });
}
