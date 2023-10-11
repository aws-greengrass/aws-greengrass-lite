#include "data/globals.h"
#include <cpp_api.hpp>

uint32_t ggapiRegisterPlugin(
    uint32_t moduleHandle,
    uint32_t componentName,
    ggapiLifecycleCallback lifecycleCallback,
    uintptr_t callbackContext
) {
    return ggapi::trapErrorReturn<size_t>(
        [moduleHandle, componentName, lifecycleCallback, callbackContext]() {
            data::Global &global = data::Global::self();
            std::shared_ptr<plugins::AbstractPlugin> parentModule{
                global.environment.handleTable.getObject<plugins::AbstractPlugin>(data::ObjHandle{
                    moduleHandle})};
            std::shared_ptr<plugins::DelegatePlugin> delegate{
                std::make_shared<plugins::DelegatePlugin>(
                    global.environment,
                    global.environment.stringTable.getString(data::StringOrd{componentName}),
                    parentModule,
                    lifecycleCallback,
                    callbackContext
                )};
            data::ObjectAnchor anchor =
                global.loader->anchor(delegate); // TODO: schedule bootstrap cycle
            return anchor.getHandle().asInt();
        }
    );
}
