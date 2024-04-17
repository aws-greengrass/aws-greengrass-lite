#include "plugin_loader.hpp"
#include "data/shared_list.hpp"
#include "deployment/deployment_manager.hpp"
#include "deployment/device_configuration.hpp"
#include "deployment/model/dependency_order.hpp"
#include "deployment/recipe_loader.hpp"
#include "deployment/recipe_model.hpp"
#include "scope/context_full.hpp"
#include "tasks/task_callbacks.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>

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

    bool NativePlugin::callNativeLifecycle(
        const data::Symbol &event, const std::shared_ptr<data::StructModelBase> &data) {

        auto *lifecycleFn = _lifecycleFn.load();
        if(lifecycleFn != nullptr) {
            scope::TempRoot tempRoot;
            bool handled = false;
            // TODO: Remove module parameter
            ggapiErrorKind error = lifecycleFn(
                scope::asIntHandle(baseRef()), event.asInt(), scope::asIntHandle(data), &handled);
            errors::Error::throwThreadError(error);
            return handled;
        } else {
            return false; // not handled
        }
    }

    bool DelegatePlugin::callNativeLifecycle(
        const data::Symbol &phase, const std::shared_ptr<data::StructModelBase> &data) {

        auto callback = _callback;
        if(callback) {
            scope::TempRoot tempRoot;
            // TODO: Remove module parameter
            return callback->invokeLifecycleCallback(ref<plugins::AbstractPlugin>(), phase, data);
        } else {
            return false; // no callback, so caller should act as if event unhandled
        }
    }

    void PluginLoader::discoverPlugins() {
        // The only plugins used are those in the plugin directory, or subdirectory of
        // plugin directory

        // Create found recipes unordered map pass.
        for(const auto &top : fs::directory_iterator(getPaths()->pluginRecipePath())) {
            if(top.is_regular_file()) {
                discoverRecipe(top);
            } else if(top.is_directory()) {
                for(const auto &fileEnt : fs::directory_iterator(top)) {
                    if(fileEnt.is_regular_file()) {
                        discoverRecipe(fileEnt);
                    }
                }
            }
        }

        // Load all found Native plugins in plugins dir, if also in recipes map pass.
        for(const auto &top : fs::directory_iterator(getPaths()->pluginPath())) {
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

        /* Noop statement for breakpoint in debugger */
        ;
    }

    void PluginLoader::discoverPlugin(const fs::directory_entry &entry) {
        if(entry.path().extension() == NATIVE_SUFFIX) {
            std::cout << entry.path().stem() << '\n';
            auto stem = entry.path().stem().generic_string();
            auto name = util::trimStart(stem, "lib");
            std::string serviceName = std::string("local.plugins.discovered.") + std::string(name);

            if(auto it = _recipePaths.find(serviceName); it != _recipePaths.end()) {
                auto plugin = loadNativePlugin(entry.path(), serviceName);
            }
        }
    }

    void PluginLoader::discoverRecipe(const fs::directory_entry &entry) {
        std::string ext = util::lower(entry.path().extension().generic_string());
        auto stem = entry.path().stem().generic_string();
        // For every recipe found, add to the recipePaths unordered map
        if(ext == ".yaml" || ext == ".yml" || ext == ".json") {
            if(auto pos = stem.find_last_of('-'); pos != std::string::npos) {
                _recipePaths.emplace(stem.substr(0, pos), entry.path());
            } else {
                _recipePaths.emplace(stem, entry.path());
            }
        }
    }

    std::shared_ptr<AbstractPlugin> PluginLoader::loadComponent(
        const deployment::Recipe &recipe,
        const std::unordered_map<std::string, std::string> &loaders) {
        std::ignore = *this;
        return {};
    }

    void PluginLoader::updateLoaders(std::unordered_map<std::string, std::string> const &loaders) {
        auto middle = std::partition(
            _missingLoader.begin(),
            _missingLoader.end(),
            [&loaders](const deployment::Recipe &recipe) -> bool {
                return loaders.find(recipe.componentType) == loaders.end();
            });

        auto broken = std::partition(
            middle,
            _missingLoader.end(),
            [this, loaders](const deployment::Recipe &recipe) -> bool {
                std::shared_ptr<AbstractPlugin> component = loadComponent(recipe, loaders);
                if(!component) {
                    return true;
                }
                std::string name = component->getName();
                _inactive.emplace(std::move(name), std::move(component));
                return false;
            });

        for(auto first = middle; first != broken; ++first) {
            // Track this somewhere?
            std::cerr << "Failed to load: " << first->getComponentName() << '\n';
        }

        _missingLoader.erase(middle, _missingLoader.end());
    }

    std::shared_ptr<AbstractPlugin> PluginLoader::loadNativePlugin(
        const std::filesystem::path &path, const std::string &serviceName) {
        LOG.atInfo().kv("path", path.string()).log("Loading native plugin");
        auto plugin{std::make_shared<NativePlugin>(context(), serviceName)};
        plugin->load(path);
        plugin->initialize(*this);
        _all.emplace(serviceName, plugin);
        _inactive.emplace(std::move(serviceName), plugin);
        return plugin;
    }

    std::vector<std::shared_ptr<AbstractPlugin>> PluginLoader::processInactiveList() {
        auto resolved = util::DependencyOrder{}.computeOrderedDependencies(
            _inactive, [](auto &&pendingService) -> const std::unordered_set<std::string> & {
                return pendingService->getDependencies();
            });
        std::vector<std::shared_ptr<AbstractPlugin>> runOrder;
        runOrder.reserve(resolved->size());
        while(!resolved->empty()) {
            runOrder.emplace_back(resolved->poll());
            _active.emplace_back(runOrder.back());
        }
        return runOrder;
    }

    void PluginLoader::forAllActivePlugins(
        const std::function<void(AbstractPlugin &, const std::shared_ptr<data::StructModelBase> &)>
            &fn) const {

        for(const auto &i : _active) {
            i->invoke(fn);
        }
    }

    PluginLoader &AbstractPlugin::loader() {
        return context()->pluginLoader();
    }

    std::shared_ptr<config::Topics> PluginLoader::getServiceTopics(AbstractPlugin &plugin) const {
        return context()->configManager().lookupTopics({SERVICES, plugin.getName()});
    }

    std::optional<deployment::Recipe> PluginLoader::loadRecipe(
        const AbstractPlugin &plugin) const noexcept {
        std::string name = plugin.getName();
        std::error_code err{};
        fs::directory_iterator dir{_paths->pluginRecipePath(), err};
        if(err) {
            return {};
        }
        // search recipe map for path and load
        if(auto it = _recipePaths.find(name); it != _recipePaths.end()) {
            auto recipePath = it->second;
            try {
                return deployment::RecipeLoader{}.read(recipePath);
            } catch(std::runtime_error &e) {
                // pass
                LOG.atWarn("recipe-not-loaded")
                    .cause(e)
                    .kv("path", recipePath.string())
                    .log("Unable to load recipe file");
            }
        }
        LOG.atWarn("recipe-not-found")
            .kv("component", plugin.getName())
            .log("Unable to locate recipe");
        return {};
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

    void AbstractPlugin::lifecycle(
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
        } catch(ggapi::EXCEPTION_UNHANDLED &e) {
            LOG.atInfo()
                .event("lifecycle-unhandled")
                .kv("name", getName())
                .kv("event", event)
                .log();
            // TODO: Add default behavior for unhandled callback
        } catch(const errors::Error &lastError) {
            LOG.atError()
                .event("lifecycle-error")
                .kv("name", getName())
                .kv("event", event)
                .cause(lastError)
                .log();
        } catch(...) { 
            LOG.atError()
                .event("lifecycle-error")
                .kv("name", getName())
                .kv("event", event)
                .kv("error", "UNKNOWN")
                .log();
        }
    }

    void AbstractPlugin::initialize(PluginLoader &loader) {
        auto data = loader.buildParams(*this, true);
        data::StructElement el = data->get(loader.NAME);
        if(el.isScalar()) {
            // Allow name to be changed
            _moduleName = el.getString();
        }
        configure(loader);
        // search for recipe file
        auto recipe = loader.loadRecipe(*this);
        // Update data, module name is now known
        data = loader.buildParams(*this, false);
        auto config = data->get(loader.CONFIG).castObject<data::StructModelBase>();
        if(!recipe) {
            // This path is only taken if the recipe is unknown
            config->put("version", std::string("0.0.0"));
            config->put("dependencies", std::make_shared<data::SharedList>(context()));
        } else {
            auto dependencies = std::make_shared<data::SharedList>(context());
            for(auto &&dependency : recipe->componentDependencies) {
                if(dependency.second.dependencyType == "HARD") {
                    dependencies->push(dependency.first);
                    _dependencies.emplace(dependency.first);
                }
            }
            config->put("version", recipe->componentVersion);
            config->put("dependencies", dependencies);
        }
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
