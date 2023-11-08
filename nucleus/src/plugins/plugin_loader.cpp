#include "plugin_loader.hpp"
#include "pubsub/local_topics.hpp"
#include "tasks/task.hpp"
#include <iostream>

namespace fs = std::filesystem;

#define STRINGIFY(x) #x
#define STRINGIFY2(x) STRINGIFY(x)
#define NATIVE_SUFFIX STRINGIFY2(PLATFORM_SHLIB_SUFFIX)

plugins::NativePlugin::~NativePlugin() {
    nativeHandle_t h = _handle.load();
    _handle.store(nullptr);
    if(!h) {
        return;
    }
#if defined(USE_DLFCN)
    ::dlclose(_handle);
#elif defined(USE_WINDLL)
    ::FreeLibrary(_handle);
#endif
}

void plugins::NativePlugin::load(const std::string &filePath) {
#if defined(USE_DLFCN)
    nativeHandle_t handle = ::dlopen(filePath.c_str(), RTLD_NOW | RTLD_LOCAL);
    _handle.store(handle);
    if(handle == nullptr) {
        // Note, dlerror() below will flag "concurrency-mt-unsafe"
        // It is thread safe on Linux and Mac
        // There is no safer alternative, so all we can do is suppress
        // TODO: When implementing loader thread, make sure this is all in same thread
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        std::string error{dlerror()};
        throw std::runtime_error(
            std::string("Cannot load shared object: ") + filePath + std::string(" ") + error
        );
    }
    // NOLINTNEXTLINE(*-reinterpret-cast)
    _lifecycleFn.store(reinterpret_cast<lifecycleFn_t>(::dlsym(_handle, "greengrass_lifecycle")));
#elif defined(USE_WINDLL)
    nativeHandle_t handle =
        ::LoadLibraryEx(filePath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    _handle.store(handle);
    if(handle == nullptr) {
        uint32_t lastError = ::GetLastError();
        // TODO: use FormatMessage
        throw std::runtime_error(
            std::string("Cannot load DLL: ") + filePath + std::string(" ")
            + std::to_string(lastError)
        );
    }
    _lifecycleFn.store(
        // NOLINTNEXTLINE(*-reinterpret-cast)
        reinterpret_cast<lifecycleFn_t>(::GetProcAddress(_handle.load(), "greengrass_lifecycle"))
    );
#endif
}

bool plugins::NativePlugin::isActive() noexcept {
    return _lifecycleFn.load() != nullptr;
}

void plugins::PluginLoader::lifecycle(
    data::StringOrd phase, const std::shared_ptr<data::StructModelBase> &data
) {
    // TODO: Run this inside of a task?
    for(const auto &i : getRoots()) {
        std::shared_ptr<AbstractPlugin> plugin{i.getObject<AbstractPlugin>()};
        if(plugin->isActive()) {
            ::ggapiSetError(0);
            // TODO: convert to logging
            std::cerr << "Plugin \"" << plugin->getName()
                      << "\" lifecycle phase: " << _environment.stringTable.getString(phase)
                      << std::endl;
            if(!plugin->lifecycle(i.getHandle(), phase, data)) {
                data::StringOrd lastError{::ggapiGetError()};
                if(lastError) {
                    std::cerr << "Plugin \"" << plugin->getName()
                              << "\" lifecycle error during phase: "
                              << _environment.stringTable.getString(phase) << " - "
                              << _environment.stringTable.getString(lastError) << std::endl;
                } else {
                    std::cerr << "Plugin \"" << plugin->getName()
                              << "\" lifecycle unhandled phase: "
                              << _environment.stringTable.getString(phase) << std::endl;
                }
            }
        }
    }
}

bool plugins::NativePlugin::lifecycle(
    data::ObjHandle pluginAnchor,
    data::StringOrd phase,
    const std::shared_ptr<data::StructModelBase> &data
) {
    lifecycleFn_t lifecycleFn = _lifecycleFn.load();
    if(lifecycleFn != nullptr) {
        data::LocalCallScope scope{_environment};
        std::shared_ptr<data::StructModelBase> copy = data->copy();
        data::ObjectAnchor dataAnchor = scope->anchor(copy);
        return lifecycleFn(pluginAnchor.asInt(), phase.asInt(), dataAnchor.getHandle().asInt());
    }
    return true; // no error
}

bool plugins::DelegatePlugin::lifecycle(
    data::ObjHandle pluginAnchor,
    data::StringOrd phase,
    const std::shared_ptr<data::StructModelBase> &data
) {
    uintptr_t delegateContext;
    ggapiLifecycleCallback delegateLifecycle;
    {
        std::shared_lock guard{_mutex};
        delegateContext = _delegateContext;
        delegateLifecycle = _delegateLifecycle;
    }
    if(delegateLifecycle != nullptr) {
        data::LocalCallScope scope{_environment};
        std::shared_ptr<data::StructModelBase> copy = data->copy();
        data::ObjectAnchor dataAnchor = scope->anchor(copy);
        return delegateLifecycle(
            delegateContext, pluginAnchor.asInt(), phase.asInt(), dataAnchor.getHandle().asInt()
        );
    }
    return true;
}

void plugins::PluginLoader::discoverPlugins(const std::filesystem::path &pluginDir) {
    // The only plugins used are those in the plugin directory, or subdirectory of
    // plugin directory
    for(const auto &top : fs::directory_iterator(pluginDir)) {
        if(top.is_regular_file()) {
            discoverPlugin(top);
        } else if(top.is_directory()) {
            for(const auto &fileEnt : fs::directory_iterator(top)) {
                if(fileEnt.is_regular_file()) {
                    discoverPlugin(fileEnt);
                }
            }
        }
    }
}

void plugins::PluginLoader::discoverPlugin(const fs::directory_entry &entry) {
    std::string name{entry.path().generic_string()};
    if(entry.path().extension().compare(NATIVE_SUFFIX) == 0) {
        loadNativePlugin(name);
        return;
    }
}

void plugins::PluginLoader::loadNativePlugin(const std::string &name) {
    std::shared_ptr<NativePlugin> plugin{std::make_shared<NativePlugin>(_environment, name)};
    std::cout << "Loading native plugin from " << name << std::endl;
    plugin->load(name);
    // add the plugins to collection by "anchoring"
    // which solves a number of interesting problems
    anchor(plugin);
}

void plugins::PluginLoader::lifecycleBootstrap(const std::shared_ptr<data::StructModelBase> &data) {
    data::Handle key = _environment.stringTable.getOrCreateOrd("bootstrap");
    lifecycle(key, data);
}

void plugins::PluginLoader::lifecycleDiscover(const std::shared_ptr<data::StructModelBase> &data) {
    data::Handle key = _environment.stringTable.getOrCreateOrd("discover");
    lifecycle(key, data);
}

void plugins::PluginLoader::lifecycleStart(const std::shared_ptr<data::StructModelBase> &data) {
    data::Handle key = _environment.stringTable.getOrCreateOrd("start");
    lifecycle(key, data);
}

void plugins::PluginLoader::lifecycleRun(const std::shared_ptr<data::StructModelBase> &data) {
    data::Handle key = _environment.stringTable.getOrCreateOrd("run");
    lifecycle(key, data);
}

void plugins::PluginLoader::lifecycleTerminate(const std::shared_ptr<data::StructModelBase> &data) {
    data::Handle key = _environment.stringTable.getOrCreateOrd("terminate");
    lifecycle(key, data);
}
