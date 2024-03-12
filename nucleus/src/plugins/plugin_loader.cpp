#include "plugin_loader.hpp"
#include "data/shared_list.hpp"
#include "deployment/device_configuration.hpp"
#include "errors/error_base.hpp"
#include "scope/context_full.hpp"
#include "tasks/task.hpp"
#include "tasks/task_callbacks.hpp"
#include <cpp_api.hpp>
#include <iostream>

namespace fs = std::filesystem;

#define STRINGIFY(x) #x
#define STRINGIFY2(x) STRINGIFY(x)
#define NATIVE_SUFFIX STRINGIFY2(PLATFORM_SHLIB_SUFFIX)

const auto LOG = // NOLINT(cert-err58-cpp)
    logging::Logger::of("com.aws.greengrass.plugins");

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
        _lifecycleFn.store(
            // NOLINTNEXTLINE(*-reinterpret-cast)
            reinterpret_cast<GgapiLifecycleFn *>(::dlsym(_handle, NATIVE_ENTRY_NAME)));
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
            reinterpret_cast<GgapiLifecycleFn *>(
                ::GetProcAddress(_handle.load(), NATIVE_ENTRY_NAME)));
#endif
    }

    bool NativePlugin::isActive() noexcept {
        return _lifecycleFn.load() != nullptr;
    }

    bool NativePlugin::callNativeLifecycle(
        const data::ObjHandle &pluginHandle,
        const data::Symbol &event,
        const data::ObjHandle &dataHandle) {

        GgapiLifecycleFn *lifecycleFn = _lifecycleFn.load();

        if(lifecycleFn != nullptr) {
            bool handled = false;
            ggapiErrorKind error =
                lifecycleFn(pluginHandle.asInt(), event.asInt(), dataHandle.asInt(), &handled);
            errors::Error::throwThreadError(error);
            return handled;
        }
        return true; // no error
    }

    bool DelegatePlugin::callNativeLifecycle(
        const data::ObjHandle &pluginRoot,
        const data::Symbol &event,
        const data::ObjHandle &dataHandle) {

        auto callback = _callback;
        if(callback) {
            return callback->invokeLifecycleCallback(pluginRoot, event, dataHandle);
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
            std::make_shared<NativePlugin>(context(), serviceName)};
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
        return context()->pluginLoader();
    }

    std::shared_ptr<config::Topics> PluginLoader::getServiceTopics(AbstractPlugin &plugin) const {
        return context()->configManager().lookupTopics({SERVICES, plugin.getName()});
    }

    std::shared_ptr<data::StructModelBase> PluginLoader::buildParams(
        AbstractPlugin &plugin, bool partial) const {
        std::string nucleusName = _deviceConfig->getNucleusComponentName();
        auto data = std::make_shared<data::SharedStruct>(context());
        data->put(CONFIG_ROOT, context()->configManager().root());
        data->put(SYSTEM, context()->configManager().lookupTopics({SYSTEM}));
        if(!partial) {
            data->put(
                NUCLEUS_CONFIG, context()->configManager().lookupTopics({SERVICES, nucleusName}));
            data->put(CONFIG, getServiceTopics(plugin));
        }
        data->put(NAME, plugin.getName());
        return data;
    }
    void PluginLoader::setPaths(const std::shared_ptr<util::NucleusPaths> &paths) {
        _paths = paths;
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
        data::Symbol event, const std::shared_ptr<data::StructModelBase> &data) {

        LOG.atInfo().event("lifecycle").kv("name", getName()).kv("event", event).log();
        errors::ThreadErrorContainer::get().clear();
        scope::StackScope scope{};
        plugins::CurrentModuleScope moduleScope(ref<AbstractPlugin>());

        data::ObjHandle dataHandle = scope.getCallScope()->root()->anchor(data).getHandle();
        try {
            if(callNativeLifecycle(getSelf(), event, dataHandle)) {
                LOG.atDebug()
                    .event("lifecycle-completed")
                    .kv("name", getName())
                    .kv("event", event)
                    .log();
            } else {
                LOG.atInfo()
                    .event("lifecycle-unhandled")
                    .kv("name", getName())
                    .kv("event", event)
                    .log();
                // TODO: Add default behavior for unhandled callback
            }
        } catch(const errors::Error &lastError) {
            LOG.atError()
                .event("lifecycle-error")
                .kv("name", getName())
                .kv("event", event)
                .cause(lastError)
                .log();
        }
    }

    void AbstractPlugin::initialize(PluginLoader &loader) {
        if(!isActive()) {
            return;
        }
        auto data = loader.buildParams(*this, true);
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
        config->put("dependencies", std::make_shared<data::SharedList>(context()));
        // Now allow plugin to bind to service part of the config tree
        lifecycle(loader.INITIALIZE, data);
    }

    void AbstractPlugin::configure(PluginLoader &loader) {
        auto serviceTopics = loader.getServiceTopics(*this);
        auto configTopics = serviceTopics->lookupTopics({loader.CONFIGURATION});
        auto loggingTopics = configTopics->lookupTopics({loader.LOGGING});
        auto &logManager = context()->logManager();
        // TODO: Register a config watcher to monitor for logging config changes
        logging::LogConfigUpdate logConfigUpdate{logManager, loggingTopics, loader.getPaths()};
        logManager.reconfigure(_moduleName, logConfigUpdate);
    }

    CurrentModuleScope::CurrentModuleScope(const std::shared_ptr<AbstractPlugin> &activeModule) {
        _old = scope::thread()->setModules(std::pair(activeModule, activeModule));
    }
    CurrentModuleScope::~CurrentModuleScope() {
        scope::thread()->setModules(_old);
    }
} // namespace plugins
