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
extern "C" [[maybe_unused]] [[maybe_unused]] EXPORT bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t data) noexcept {
    return LayeredPlugin::get().lifecycle(moduleHandle, phase, data);
}

void greengrass_delegate_lifecycle(
    ggapi::ModuleScope moduleHandle, ggapi::StringOrd phase, ggapi::Struct data) {
    std::cout << "Running lifecycle getDelegate... " << moduleHandle.getHandleId() << " phase "
              << phase.toString() << std::endl;
    LayeredPlugin::get().getDelegate(moduleHandle).lifecycle(moduleHandle, phase, data);
}

bool DelegatePlugin::onStart(ggapi::Struct data) {
    std::cout << "Running getDelegate start... " << std::endl;
    return true;
}

bool LayeredPlugin::onDiscover(ggapi::Struct data) {
    std::cout << "Layered Plugin: Running lifecycle discovery" << std::endl;
    std::unique_lock guard{_mutex};
    ggapi::ObjHandle nestedPlugin =
        getScope().registerPlugin(ggapi::StringOrd{"MyDelegate"}, greengrass_delegate_lifecycle);
    std::unique_ptr<DelegatePlugin> plugin{std::make_unique<DelegatePlugin>()};
    _delegates.emplace(nestedPlugin.getHandleId(), std::move(plugin));
    return true;
}
