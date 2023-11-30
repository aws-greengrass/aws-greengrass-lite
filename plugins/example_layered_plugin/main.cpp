#include <iostream>
#include <map>
#include <mutex>
#include <plugin.hpp>
#include <vector>

// A layered plugins is permitted to add additional abstract plugins

class DelegatePlugin : public ggapi::Plugin {
public:
    bool onStart(ggapi::Struct data) override;
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

// This could sit in the stub, but in this use-case, is needed outside of the stub

//
// Recommended stub
//
bool greengrass_lifecycle(uint32_t moduleHandle, uint32_t phase, uint32_t data) noexcept {
    return LayeredPlugin::get().lifecycle(moduleHandle, phase, data);
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
    ggapi::ObjHandle nestedPlugin = getScope().registerPlugin(
        ggapi::StringOrd{"MyDelegate"},
        [&ref](ggapi::ModuleScope moduleScope, ggapi::Symbol phase, ggapi::Struct data) {
            return ref.lifecycle(moduleScope, phase, data);
        });
    _delegates.emplace(nestedPlugin.getHandleId(), std::move(plugin));
    return true;
}
