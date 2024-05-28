#include "plugin_loader.hpp"
#include "api_standard_errors.hpp"
#include "data/shared_list.hpp"
#include "deployment/deployment_manager.hpp"
#include "deployment/device_configuration.hpp"
#include "deployment/model/dependency_order.hpp"
#include "deployment/recipe_model.hpp"
#include "errors/error_base.hpp"
#include "package_manager/recipe_loader.hpp"
#include "scope/context_full.hpp"
#include "string_util.hpp"
#include "tasks/task_callbacks.hpp"
#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// Two macro invocations are required to stringify a macro's value
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define STRINGIFY(x) #x
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define STRINGIFY2(x) STRINGIFY(x)
inline constexpr std::string_view NATIVE_SUFFIX = STRINGIFY2(PLATFORM_SHLIB_SUFFIX);

static const auto LOG = // NOLINT(cert-err58-cpp)
    logging::Logger::of("com.aws.greengrass.plugins");

namespace plugins {
#if defined(USE_WINDLL)
    struct LocalStringDeleter {
        void operator()(LPSTR p) const noexcept {
            // Free the Win32's string's buffer.
            LocalFree(p);
        }
    };
#endif

    static std::runtime_error makePluginError(
        std::string_view description, const std::filesystem::path &path, std::string_view message) {
        // TODO, this should be returning a GG error
        const auto pathStr = path.string();
        std::string what;
        what.reserve(description.size() + pathStr.size() + message.size() + 2U);
        what.append(description).append(pathStr).push_back(' ');
        what.append(message);
        return std::runtime_error{what};
    }

    static std::string getLastPluginError() {
#if defined(USE_DLFCN)
        // Note, dlerror() below will flag "concurrency-mt-unsafe"
        // It is thread safe on Linux and Mac
        // There is no safer alternative, so all we can do is suppress
        // TODO: When implementing loader thread, make sure this is all in same thread
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        const char *error = dlerror();
        if(!error) {
            return {};
        }
        return error;
#elif defined(USE_WINDLL)
        // look up error message from system message table. Leave string unformatted.
        // https://devblogs.microsoft.com/oldnewthing/20071128-00/?p=24353
        DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                      | FORMAT_MESSAGE_IGNORE_INSERTS;
        DWORD lastError = ::GetLastError();
        LPSTR messageBuffer = nullptr;
        DWORD size = FormatMessageA(
            flags,
            NULL,
            lastError,
            0,
            // pointer type changes when FORMAT_MESSAGE_ALLOCATE_BUFFER is specified
            // NOLINTNEXTLINE(*-reinterpret-cast)
            reinterpret_cast<LPTSTR>(&messageBuffer),
            0,
            NULL);
        // Take exception-safe ownership
        using char_type = std::remove_pointer_t<LPSTR>;
        std::unique_ptr<char_type, LocalStringDeleter> owner{messageBuffer};

        // fallback
        if(size == 0) {
            return "Error Code " + std::to_string(lastError);
        }

        // copy message buffer from Windows string
        return std::string{messageBuffer, size};
#endif
    }

    NativePlugin::~NativePlugin() noexcept {
        NativeHandle h = _handle.load();
        if(!h) {
            return;
        }
#if defined(USE_DLFCN)
        ::dlclose(h);
#elif defined(USE_WINDLL)
        ::FreeLibrary(h);
#endif
    }

    void NativePlugin::load(const std::filesystem::path &path) {
        std::string filePathString = path.generic_string();
#if defined(USE_DLFCN)
        NativeHandle handle = ::dlopen(filePathString.c_str(), RTLD_NOW | RTLD_LOCAL);
        _handle.store(handle);
        if(handle == nullptr) {
            // Note, dlerror() below will flag "concurrency-mt-unsafe"
            // It is thread safe on Linux and Mac
            // There is no safer alternative, so all we can do is suppress
            // TODO: When implementing loader thread, make sure this is all in same thread
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            std::string error{dlerror()};
            throw std::runtime_error(
                std::string("Cannot load shared object: ") + filePathString + std::string(" ")
                + error);
        }
        _lifecycleFn.store(
            // NOLINTNEXTLINE(*-reinterpret-cast)
            reinterpret_cast<GgapiLifecycleFn *>(::dlsym(_handle, NATIVE_ENTRY_NAME)));
#elif defined(USE_WINDLL)
        NativeHandle handle =
            ::LoadLibraryEx(filePathString.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        _handle.store(handle);
        if(handle == nullptr) {
            LOG.atError().logAndThrow(
                makePluginError("Cannot load Plugin: ", path, getLastPluginError()));
        }
#endif

#if defined(USE_DLFCN)
        auto *lifecycleFn = ::dlsym(handle, NATIVE_ENTRY_NAME);
#elif defined(USE_WINDLL)
        auto *lifecycleFn = ::GetProcAddress(handle, NATIVE_ENTRY_NAME);
#endif
        if(lifecycleFn == nullptr) {
            LOG.atWarn("lifecycle-unknown")
                .cause(
                    makePluginError("Cannot link lifecycle function: ", path, getLastPluginError()))
                .log();
        }
        // Function pointer from C-APIs which type-erase their return values.
        // NOLINTNEXTLINE(*-reinterpret-cast)
        _lifecycleFn.store(reinterpret_cast<lifecycleFn_t>(lifecycleFn));
    }

    bool NativePlugin::isActive() const noexcept {
        return _lifecycleFn.load() != nullptr;
    }

    void NativePlugin::callNativeLifecycle(
        const data::Symbol &event, const std::shared_ptr<data::StructModelBase> &data) {

        auto *lifecycleFn = _lifecycleFn.load();
        assert(lifecycleFn);
        scope::TempRoot tempRoot;
        // TODO: Remove module parameter
        ggapiErrorKind error =
            lifecycleFn(scope::asIntHandle(baseRef()), event.asInt(), scope::asIntHandle(data));
        errors::Error::throwThreadError(error);
    }

    void DelegatePlugin::callNativeLifecycle(
        const data::Symbol &phase, const std::shared_ptr<data::StructModelBase> &data) {
        assert(_callback);
        _callback->invokeLifecycleCallback(ref<plugins::AbstractPlugin>(), phase, data);
    }

    std::vector<deployment::Recipe> PluginLoader::discoverComponents() {
        // The only plugins used are those in the plugin directory, or subdirectory of
        // plugin directory

        // Create found recipes unordered map pass.
        std::vector<deployment::Recipe> recipes;
        for(const auto &top : fs::directory_iterator(getPaths()->pluginRecipePath())) {
            if(top.is_regular_file()) {
                if(auto recipe = discoverRecipe(top)) {
                    recipes.emplace_back(recipe.value());
                }
            } else if(top.is_directory()) {
                for(const auto &fileEnt : fs::directory_iterator(top)) {
                    if(fileEnt.is_regular_file()) {
                        if(auto recipe = discoverRecipe(fileEnt)) {
                            recipes.emplace_back(recipe.value());
                        }
                    }
                }
            }
        }

        return recipes;
    }

    std::optional<deployment::Recipe> PluginLoader::discoverRecipe(
        const fs::directory_entry &entry) {
        std::string ext = util::lower(entry.path().extension().generic_string());
        auto stem = entry.path().stem().generic_string();
        // For every recipe found, add to the recipe list
        if(ext == ".yaml" || ext == ".yml" || ext == ".json") {
            try {
                // TODO: move component config behavior into deployment manager
                auto recipe = package_manager::RecipeLoader{}.read(entry);
                auto serviceTopic =
                    context()->configManager().lookupTopics({SERVICES, recipe.componentName});
                serviceTopic->put("recipePath", entry.path().generic_string());
                return recipe;
            } catch(const std::exception &e) {
                LOG.atError()
                    .cause(e)
                    .kv("path", entry.path().generic_string())
                    .log("Failed to load recipe");
            }
        }
        return {};
    }
    std::shared_ptr<AbstractPlugin> PluginLoader::loadNativePlugin(
        const deployment::Recipe &recipe) {
        LOG.atInfo().kv("component", recipe.componentName).log("loading native plugin");
        auto plugin = std::make_shared<NativePlugin>(context(), recipe);
        using namespace std::string_literals;
        // TODO: load first shared object in plugin artifact directory
        // auto artifactDir = getPaths()->pluginPath() / "artifacts" / recipe.componentName /
        // recipe.componentVersion;
        auto name = util::splitWith(recipe.componentName, '.').back();
        plugin->load(getPaths()->pluginPath() / ("lib"s + name + std::string{NATIVE_SUFFIX}));
        plugin->configure(*this);
        auto config = getServiceTopics(*plugin);
        auto dependencies = std::make_shared<data::SharedList>(context());
        for(const auto &[service, type] : recipe.componentDependencies) {
            dependencies->push(service);
        }
        config->put("version", recipe.componentVersion);
        config->put("dependencies", dependencies);
        return plugin;
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
        data->put(MODULE, plugin.ref<AbstractPlugin>());
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

    bool AbstractPlugin::lifecycle(
        data::Symbol event, const std::shared_ptr<data::StructModelBase> &data) {

        LOG.atInfo().event("lifecycle").kv("name", getName()).kv("event", event).log();
        errors::ThreadErrorContainer::get().clear();
        plugins::CurrentModuleScope moduleScope(ref<AbstractPlugin>());

        try {
            callNativeLifecycle(event, data);
            LOG.atDebug()
                .event("lifecycle-completed")
                .kv("name", getName())
                .kv("event", event)
                .log();
            return true;
        } catch(ggapi::UnhandledLifecycleEvent &e) {
            LOG.atInfo()
                .event("lifecycle-unhandled")
                .kv("name", getName())
                .kv("event", event)
                .log();
            // TODO: Add default behavior for unhandled callback
            return true;
        } catch(const errors::Error &lastError) {
            LOG.atError()
                .event("lifecycle-error")
                .kv("name", getName())
                .kv("event", event)
                .cause(lastError)
                .log();
            return false;
        }
    }

    void AbstractPlugin::initialize(PluginLoader &loader) {
        configure(loader);
        // search for recipe file
        const auto &recipe = _recipe;
        // Update data, module name is now known
        auto data = loader.buildParams(*this, false);
        auto config = data->get(loader.CONFIG).castObject<data::StructModelBase>();
        auto dependencies = std::make_shared<data::SharedList>(context());
        for(auto &&dependency : recipe.componentDependencies) {
            dependencies->push(dependency.first);
        }
        config->put("version", recipe.componentVersion);
        config->put("dependencies", dependencies);
    }

    void AbstractPlugin::configure(PluginLoader &loader) {
        auto serviceTopics = loader.getServiceTopics(*this);
        auto configTopics = serviceTopics->lookupTopics({loader.CONFIGURATION});
        auto loggingTopics = configTopics->lookupTopics({loader.LOGGING});
        auto &logManager = context()->logManager();
        // TODO: Register a config watcher to monitor for logging config changes
        logging::LogConfigUpdate logConfigUpdate{logManager, loggingTopics, loader.getPaths()};
        logManager.reconfigure(_recipe.componentName, logConfigUpdate);
    }

    CurrentModuleScope::CurrentModuleScope(const std::shared_ptr<AbstractPlugin> &activeModule) {
        _old = scope::thread()->setModules(std::pair(activeModule, activeModule));
    }
    CurrentModuleScope::~CurrentModuleScope() {
        scope::thread()->setModules(_old);
    }
} // namespace plugins
