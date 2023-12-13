#include "plugin_loader.hpp"
#include "deployment/device_configuration.hpp"
#include "errors/error_base.hpp"
#include "scope/context_full.hpp"
#include "tasks/task.hpp"
#include "tasks/task_callbacks.hpp"
#include <iostream>

namespace fs = std::filesystem;

#define STRINGIFY(x) #x
#define STRINGIFY2(x) STRINGIFY(x)
#define NATIVE_SUFFIX STRINGIFY2(PLATFORM_SHLIB_SUFFIX)

namespace plugins {

    NativePlugin::~NativePlugin() {
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

    void NativePlugin::load(const std::filesystem::path &path) {
        std::string filePath = path.generic_string();
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
                std::string("Cannot load shared object: ") + filePath + std::string(" ") + error);
        }
        // NOLINTNEXTLINE(*-reinterpret-cast)
        _lifecycleFn.store(reinterpret_cast<lifecycleFn_t>(::dlsym(_handle, NATIVE_ENTRY_NAME)));
#elif defined(USE_WINDLL)
        nativeHandle_t handle =
            ::LoadLibraryEx(filePath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        _handle.store(handle);
        if(handle == nullptr) {
            uint32_t lastError = ::GetLastError();
            // TODO: use FormatMessage
            throw std::runtime_error(
                std::string("Cannot load DLL: ") + filePath + std::string(" ")
                + std::to_string(lastError));
        }
        _lifecycleFn.store(
            // NOLINTNEXTLINE(*-reinterpret-cast)
            reinterpret_cast<lifecycleFn_t>(::GetProcAddress(_handle.load(), NATIVE_ENTRY_NAME)));
#endif
    }

    bool NativePlugin::isActive() noexcept {
        return _lifecycleFn.load() != nullptr;
    }

    bool NativePlugin::callNativeLifecycle(
        const data::ObjHandle &pluginHandle,
        const data::Symbol &phase,
        const data::ObjHandle &dataHandle) {

        lifecycleFn_t lifecycleFn = _lifecycleFn.load();
        if(lifecycleFn != nullptr) {
            return lifecycleFn(pluginHandle.asInt(), phase.asInt(), dataHandle.asInt());
        }
        return true; // no error
    }

    bool DelegatePlugin::callNativeLifecycle(
        const data::ObjHandle &pluginHandle,
        const data::Symbol &phase,
        const data::ObjHandle &dataHandle) {

        auto callback = _callback;
        if(callback) {
            return callback->invokeLifecycleCallback(pluginHandle, phase, dataHandle);
        }
        return true;
    }

    void PluginLoader::discoverPlugins(const std::filesystem::path &pluginDir) {
        // The only plugins used are those in the plugin directory, or subdirectory of
        // plugin directory
        // TODO: This is temporary logic until recipe logic has been written
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

    void PluginLoader::discoverPlugin(const fs::directory_entry &entry) {
        if(entry.path().extension().compare(NATIVE_SUFFIX) == 0) {
            loadNativePlugin(entry.path());
            return;
        }
    }

    void PluginLoader::loadNativePlugin(const std::filesystem::path &path) {
        auto stem = path.stem().generic_string();
        auto name = util::trimStart(stem, "lib");
        std::string serviceName = std::string("local.plugins.discovered.") + std::string(name);
        std::shared_ptr<NativePlugin> plugin{
            std::make_shared<NativePlugin>(_context.lock(), serviceName)};
        std::cout << "Loading native plugin from " << path << std::endl;
        plugin->load(path);
        // add the plugins to a collection by "anchoring"
        // which solves a number of interesting problems
        auto anchor = _root->anchor(plugin);
        plugin->setSelf(anchor.getHandle());
        plugin->initialize(*this);
    }

    void PluginLoader::forAllPlugins(
        const std::function<void(AbstractPlugin &, const std::shared_ptr<data::StructModelBase> &)>
            &fn) const {

        for(const auto &i : _root->getRoots()) {
            std::shared_ptr<AbstractPlugin> plugin{i.getObject<AbstractPlugin>()};
            plugin->invoke(fn);
        }
    }

    PluginLoader &AbstractPlugin::loader() {
        return context().pluginLoader();
    }

    std::shared_ptr<config::Topics> PluginLoader::getServiceTopics(AbstractPlugin &plugin) const {
        return context().configManager().lookupTopics({SERVICES, plugin.getName()});
    }

    std::shared_ptr<data::StructModelBase> PluginLoader::buildParams(
        AbstractPlugin &plugin, bool partial) const {
        std::string nucleusName = _deviceConfig->getNucleusComponentName();
        auto data = std::make_shared<data::SharedStruct>(_context.lock());
        data->put(CONFIG_ROOT, context().configManager().root());
        data->put(SYSTEM, context().configManager().lookupTopics({SYSTEM}));
        if(!partial) {
            data->put(
                NUCLEUS_CONFIG, context().configManager().lookupTopics({SERVICES, nucleusName}));
            data->put(CONFIG, getServiceTopics(plugin));
        }
        data->put(NAME, plugin.getName());
        return data;
    }
    void PluginLoader::setKernel(const std::shared_ptr<lifecycle::Kernel> &kernel) {
        _kernel = kernel;
    }

    void AbstractPlugin::invoke(
        const std::function<void(AbstractPlugin &, const std::shared_ptr<data::StructModelBase> &)>
            &fn) {

        if(!isActive()) {
            return;
        }
        std::shared_ptr<data::StructModelBase> data = loader().buildParams(*this);
        fn(*this, data);
    }

    void AbstractPlugin::lifecycle(
        data::Symbol phase, const std::shared_ptr<data::StructModelBase> &data) {

        errors::ThreadErrorContainer::get().clear();
        scope::StackScope scope{};
        plugins::CurrentModuleScope moduleScope(ref<AbstractPlugin>());

        data::ObjHandle dataHandle = scope.getCallScope()->root()->anchor(data).getHandle();
        if(!callNativeLifecycle(getSelf(), phase, dataHandle)) {
            std::optional<errors::Error> lastError{errors::ThreadErrorContainer::get().getError()};
            if(lastError.has_value()) {
                std::cerr << "Plugin \"" << getName()
                          << "\" lifecycle error during phase: " << phase.toString() << " - "
                          << lastError.value().what() << std::endl;
            } else {
                std::cerr << "Plugin \"" << getName()
                          << "\" lifecycle unhandled phase: " << phase.toString() << std::endl;
            }
        }
    }

    void AbstractPlugin::initialize(PluginLoader &loader) {
        if(!isActive()) {
            return;
        }
        auto data = loader.buildParams(*this, true);
        lifecycle(loader.BOOTSTRAP, data);
        data::StructElement el = data->get(loader.NAME);
        if(el.isScalar()) {
            // Allow name to be changed
            _moduleName = el.getString();
        }
        configure(loader);
        // Update data, module name is now known
        // TODO: This path is only when recipe is unknown
        data = loader.buildParams(*this, false);
        auto config = data->get(loader.CONFIG).castObject<data::StructModelBase>();
        config->put("version", std::string("0.0.0"));
        config->put("dependencies", std::make_shared<data::SharedList>(_context.lock()));
        // Now allow plugin to bind to service part of the config tree
        lifecycle(loader.BIND, data);
    }

    void AbstractPlugin::configure(PluginLoader &loader) {
        auto serviceTopics = loader.getServiceTopics(*this);
        auto configTopics = serviceTopics->lookupTopics({loader.CONFIGURATION});
        auto loggingTopics = configTopics->lookupTopics({loader.LOGGING});
        auto &logManager = context().logManager();
        auto paths = loader.kernel().getPaths();
        // TODO: Register a config watcher to monitor for logging config changes
        logging::LogConfigUpdate logConfigUpdate{logManager, loggingTopics, paths};
        logManager.reconfigure(_moduleName, logConfigUpdate);
    }

    CurrentModuleScope::CurrentModuleScope(const std::shared_ptr<AbstractPlugin> &activeModule) {
        _old = scope::Context::thread().setModules(std::pair(activeModule, activeModule));
    }
    CurrentModuleScope::~CurrentModuleScope() {
        scope::Context::thread().setModules(_old);
    }
} // namespace plugins
