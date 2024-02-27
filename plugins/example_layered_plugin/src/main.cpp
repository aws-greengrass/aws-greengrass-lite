#include <iostream>
#include <map>
#include <mutex>
#include <plugin.hpp>
#include <vector>

// A layered plugins is permitted to add additional abstract plugins

class DelegatePlugin : public ggapi::Plugin {
public:
    bool onStart(ggapi::Struct data) override;
    bool callback(
        ggapi::Symbol name,
        ggapi::ModuleScope moduleScope,
        ggapi::Symbol phase,
        ggapi::Struct data) {
        // Example of using the callback to introduce extra data
        std::ignore = name;
        return lifecycle(moduleScope, phase, data);
    }
};

class LayeredPlugin : public ggapi::Plugin {
    mutable std::mutex _mutex;
    std::map<uint32_t, std::unique_ptr<DelegatePlugin>> _delegates;

public:
    bool onDiscover(ggapi::Struct data) override;

    static LayeredPlugin &get() {
        static LayeredPlugin instance{};
        return instance;
    }

    DelegatePlugin &getDelegate(ggapi::Scope scope) {
        std::unique_lock guard{_mutex};
        return *_delegates[scope.getHandleId()];
    }
};

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data, bool *pHandled) noexcept {
    return LayeredPlugin::get().lifecycle(moduleHandle, phase, data, pHandled);
}

bool DelegatePlugin::onStart(ggapi::Struct data) {
    std::cout << "Running getDelegate start... " << std::endl;
    return true;
}

bool LayeredPlugin::onDiscover(ggapi::Struct data) {
    std::cout << "Layered Plugin: Running lifecycle discovery" << std::endl;
    std::unique_lock guard{_mutex};
    std::unique_ptr<DelegatePlugin> plugin{std::make_unique<DelegatePlugin>()};
    DelegatePlugin &ref = *plugin;
    auto name = ggapi::Symbol("MyDelegate");
    ggapi::ObjHandle nestedPlugin = getScope().registerPlugin(
        name, ggapi::LifecycleCallback::of(&DelegatePlugin::callback, &ref, name));
    _delegates.emplace(nestedPlugin.getHandleId(), std::move(plugin));
    return true;
}
