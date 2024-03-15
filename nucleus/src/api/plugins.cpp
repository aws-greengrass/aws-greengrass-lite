#include "api_error_trap.hpp"
#include "plugins/plugin_loader.hpp"
#include "scope/context_full.hpp"
#include "tasks/task_callbacks.hpp"
#include <cpp_api.hpp>

uint32_t ggapiRegisterPlugin(
    uint32_t moduleHandleInt, uint32_t componentNameInt, uint32_t callback) noexcept {
    return ggapi::trapErrorReturn<size_t>([moduleHandleInt, componentNameInt, callback]() {
        auto context = scope::context();
        // Name of new plugin component
        auto componentName = context->symbolFromInt(componentNameInt);
        auto parentModule{context->objFromInt<plugins::AbstractPlugin>(moduleHandleInt)};
        auto lifecycleCallback{context->objFromInt<tasks::Callback>(callback)};
        auto delegate{std::make_shared<plugins::DelegatePlugin>(
            context, componentName.toString(), parentModule, lifecycleCallback)};
        data::RootHandle *pRoot; // Safe because we don't deref anything
        if(parentModule) {
            pRoot = &parentModule->root(); // Module is rooted to a parent
        } else {
            pRoot = &context->pluginLoader().root(); // Module is global
        }
        auto handle = context->handles().create(delegate, *pRoot);
        return handle.asInt();
    });
}

/**
 * Change module context in the call - used by parent modules for delegate modules
 */
ggapiErrorKind ggapiChangeModule(
    ggapiObjHandle moduleHandleIn, ggapiObjHandle *pPrevHandle) noexcept {
    return apiImpl::catchErrorToKind([moduleHandleIn, pPrevHandle]() {
        auto context = scope::context();
        auto targetModule{context->objFromInt<plugins::AbstractPlugin>(moduleHandleIn)};
        auto prev = scope::thread()->setEffectiveModule(targetModule);
        if(pPrevHandle != nullptr) {
            *pPrevHandle = 0;
            if(targetModule) {
                // this could create a temporary reference cycle
                // scope->root->scope-handle->scope
                // TODO: Deep dive into solutions later
                *pPrevHandle = scope::asIntHandle(prev);
            } else {
                // We cannot root handles to the module, as we've dropped to global (Nucleus)
                // context Safest thing here is to assume this is a release
                // TODO: Revisit this in future
            }
        }
    });
}

/**
 * Retrieve the current module context for the call
 */
ggapiErrorKind ggapiGetCurrentModule(ggapiObjHandle *pHandle) noexcept {
    return apiImpl::catchErrorToKind([pHandle]() {
        auto effective = scope::thread()->getEffectiveModule();
        *pHandle = scope::asIntHandle(effective); // this will create a temporary reference cycle
                                                  // scope->root->scope-handle->scope
    });
}
