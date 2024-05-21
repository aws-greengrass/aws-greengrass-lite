#pragma once

#include "data/struct_model.hpp"
#include "deployment/recipe_model.hpp"
#include "scope/context.hpp"
#include <atomic>
#include <cpp_api.hpp>
#include <filesystem>
#include <memory>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if defined(USE_DLFCN)
#include <dlfcn.h>
#elif defined(USE_WINDLL)
#include <windows.h>
#endif

namespace config {
    class Topics;
}

namespace deployment {
    class DeviceConfiguration;
    struct Recipe;
} // namespace deployment

namespace tasks {
    class Callback;
}

namespace util {
    class NucleusPaths;
} // namespace util

namespace plugins {
    class PluginLoader;

    //
    // Abstract plugins also act as a global anchor for the given plugins module
    //
    class AbstractPlugin : public data::TrackingScope {
    protected:
        deployment::Recipe _recipe;

        static deployment::Recipe recipeFromName(std::string name) {
            deployment::Recipe recipe;
            recipe.componentName = std::move(name);
            return recipe;
        }

    public:
        using BadCastError = errors::InvalidModuleError;

        AbstractPlugin(const scope::UsingContext &context, deployment::Recipe recipe)
            : TrackingScope(context), _recipe{std::move(recipe)} {
        }
        // TODO: TempModules can't create a recipe struct without an existing module...
        // Should there just be a default Kernel module for Nucleus-scoped objects?
        AbstractPlugin(const scope::UsingContext &context, std::string name)
            : AbstractPlugin{context, recipeFromName(std::move(name))} {
        }

        virtual void callNativeLifecycle(
            const data::Symbol &event, const std::shared_ptr<data::StructModelBase> &data) = 0;

        bool lifecycle(data::Symbol event, const std::shared_ptr<data::StructModelBase> &data);

        void configure(PluginLoader &loader);

        virtual bool isActive() const noexcept {
            return true;
        }

        [[nodiscard]] const std::string &getName() const noexcept {
            return _recipe.componentName;
        }

        std::unordered_set<std::string> getDependencies() const {
            const auto &dependencies = _recipe.componentDependencies;
            auto ret = std::unordered_set<std::string>{};
            ret.reserve(dependencies.size());
            for(const auto &[k, v] : dependencies) {
                ret.emplace(k);
            }
            return ret;
        }

        void invoke(
            const std::function<void(
                plugins::AbstractPlugin &, const std::shared_ptr<data::StructModelBase> &)> &fn);

        void initialize(PluginLoader &loader);
        void setRecipe(deployment::Recipe recipe) noexcept {
            _recipe = std::move(recipe);
        }

        PluginLoader &loader();
    };

    /**
     * Delegate plugins are managed by a parent (typically native) plugins. The delegate can
     * also be used to provide handles for testing.
     */
    class DelegatePlugin : public AbstractPlugin {
        //
        // TODO: Resolve a dependency dilemma
        // References Parent->root->Delegate - so Parent has ref-count on delegate
        // therefore _parent below is weak (back) reference
        // However it's possible parent goes away. We need to ensure that if parent goes
        // away, all delegates go away too.
        //
    private:
        mutable std::shared_mutex _mutex;
        const std::weak_ptr<AbstractPlugin> _parent;
        const std::shared_ptr<tasks::Callback> _callback;

    public:
        explicit DelegatePlugin(
            const scope::UsingContext &context,
            std::string name,
            const std::shared_ptr<AbstractPlugin> &parent,
            const std::shared_ptr<tasks::Callback> &callback)
            : AbstractPlugin(context, std::move(name)), _parent{parent}, _callback{callback} {
        }

        void callNativeLifecycle(
            const data::Symbol &phase, const std::shared_ptr<data::StructModelBase> &data) override;

        std::shared_ptr<AbstractPlugin> getParent() noexcept {
            return _parent.lock();
        }
    };

    //
    // Native plugins are first-class, handled by the Nucleus itself
    //
    class NativePlugin : public AbstractPlugin {
    public:
        static constexpr auto NATIVE_ENTRY_NAME = "greengrass_lifecycle";

    private:
#if defined(USE_DLFCN)
        using NativeHandle = void *;
#elif defined(USE_WINDLL)
        using NativeHandle = HINSTANCE;
#endif
        using lifecycleFn_t = GgapiLifecycleFn *;
        std::atomic<NativeHandle> _handle{nullptr};
        std::atomic<lifecycleFn_t> _lifecycleFn{nullptr};

    public:
        explicit NativePlugin(const scope::UsingContext &context, deployment::Recipe recipe)
            : AbstractPlugin(context, std::move(recipe)) {
        }

        NativePlugin(const NativePlugin &) = delete;
        NativePlugin(NativePlugin &&) noexcept = delete;
        NativePlugin &operator=(const NativePlugin &) = delete;
        NativePlugin &operator=(NativePlugin &&) noexcept = delete;
        ~NativePlugin() noexcept override;
        void load(const std::filesystem::path &path);
        void callNativeLifecycle(
            const data::Symbol &event, const std::shared_ptr<data::StructModelBase> &data) override;
        bool isActive() const noexcept override;
    };

    /**
     * Loader is responsible for handling all plugins
     */
    class PluginLoader : protected scope::UsesContext {
    private:
        std::shared_ptr<util::NucleusPaths> _paths;
        data::RootHandle _root;
        std::shared_ptr<deployment::DeviceConfiguration> _deviceConfig;

    public:
        /*
         * initialize/install the plugin
         */
        data::SymbolInit INITIALIZE{"initialize"};
        /**
         * Request to START.  The plugin is expected to be running after this event
         */
        data::SymbolInit START{"start"};
        /**
         * Plugin component to STOP.  Shutdown any threads and free memory.  The plugin may be
         * unloaded or restarted.
         */
        data::SymbolInit STOP{"stop"};
        /**
         * Root of configuration tree (used by special plugins only)
         */
        data::SymbolInit CONFIG_ROOT{"configRoot"};
        /**
         * Plugin specific configuration (services/component-name)
         */
        data::SymbolInit CONFIG{"config"};
        /**
         * Nucleus configuration (services/nucleus-name)
         */
        data::SymbolInit NUCLEUS_CONFIG{"nucleus"};
        /**
         * Component name
         */
        data::SymbolInit NAME{"name"};
        /**
         * Module handle
         */
        data::SymbolInit MODULE{"module"};

        data::SymbolInit SERVICES{"services"};
        data::SymbolInit SYSTEM{"system"};
        data::SymbolInit CONFIGURATION{"configuration"};
        data::SymbolInit LOGGING{"logging"};

        explicit PluginLoader(const scope::UsingContext &context)
            : scope::UsesContext(context), _root(context.newRootHandle()) {
            data::SymbolInit::init(
                context,
                {
                    &INITIALIZE,
                    &START,
                    &STOP,
                    &CONFIG_ROOT,
                    &CONFIG,
                    &NUCLEUS_CONFIG,
                    &NAME,
                    &SERVICES,
                    &SYSTEM,
                    &CONFIGURATION,
                    &LOGGING,
                });
        }

        data::RootHandle &root() {
            return _root;
        }

        std::shared_ptr<config::Topics> getServiceTopics(AbstractPlugin &plugin) const;
        std::shared_ptr<data::StructModelBase> buildParams(
            plugins::AbstractPlugin &plugin, bool partial = false) const;

        std::vector<deployment::Recipe> discoverComponents();
        std::optional<deployment::Recipe> discoverRecipe(
            const std::filesystem::directory_entry &entry);

        std::shared_ptr<AbstractPlugin> loadNativePlugin(const deployment::Recipe &recipe);

        void setDeviceConfiguration(
            const std::shared_ptr<deployment::DeviceConfiguration> &deviceConfig) {
            _deviceConfig = deviceConfig;
        }
        void setPaths(const std::shared_ptr<util::NucleusPaths> &paths);
        std::shared_ptr<util::NucleusPaths> getPaths() {
            return _paths;
        }
    };

    /**
     * Ensures thread data contains information about current/context module, and performs RII
     * cleanup
     */
    class CurrentModuleScope {
    private:
        std::pair<std::shared_ptr<AbstractPlugin>, std::shared_ptr<AbstractPlugin>> _old;

    public:
        explicit CurrentModuleScope(const std::shared_ptr<AbstractPlugin> &activeModule);
        CurrentModuleScope(const CurrentModuleScope &) = delete;
        CurrentModuleScope(CurrentModuleScope &&) = delete;
        CurrentModuleScope &operator=(const CurrentModuleScope &) = delete;
        CurrentModuleScope &operator=(CurrentModuleScope &&) = delete;
        ~CurrentModuleScope();
    };

} // namespace plugins
