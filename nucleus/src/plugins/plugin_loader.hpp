#pragma once
#include "data/handle_table.hpp"
#include "data/safe_handle.hpp"
#include "data/shared_struct.hpp"
#include "scope/context.hpp"
#include <atomic>
#include <cpp_api.hpp>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
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
}

namespace tasks {
    class Callback;
}

namespace util {
    class NucleusPaths;
}

namespace plugins {
    class PluginLoader;

    //
    // Abstract plugins also act as a global anchor for the given plugins module
    //
    class AbstractPlugin : public data::TrackingScope {
    protected:
        std::string _moduleName;
        data::ObjHandle _self;

    public:
        using BadCastError = errors::InvalidModuleError;

        explicit AbstractPlugin(const scope::UsingContext &context, const std::string_view &name)
            : TrackingScope(context), _moduleName(name) {
        }

        virtual bool callNativeLifecycle(
            const data::ObjHandle &pluginRoot,
            const data::Symbol &event,
            const data::ObjHandle &dataHandle) = 0;

        void lifecycle(data::Symbol event, const std::shared_ptr<data::StructModelBase> &data);

        void configure(PluginLoader &loader);

        virtual bool isActive() noexcept {
            return true;
        }

        [[nodiscard]] std::string getName() {
            return _moduleName;
        }

        data::ObjHandle getSelf() {
            return _self;
        }

        void setSelf(const data::ObjHandle &self) {
            _self = self;
        }

        void invoke(
            const std::function<void(
                plugins::AbstractPlugin &, const std::shared_ptr<data::StructModelBase> &)> &fn);

        void initialize(PluginLoader &loader);
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
            std::string_view name,
            const std::shared_ptr<AbstractPlugin> &parent,
            const std::shared_ptr<tasks::Callback> &callback)
            : AbstractPlugin(context, name), _parent{parent}, _callback{callback} {
        }

        bool callNativeLifecycle(
            const data::ObjHandle &pluginRoot,
            const data::Symbol &event,
            const data::ObjHandle &dataHandle) override;

        std::shared_ptr<AbstractPlugin> getParent() {
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
        typedef void *nativeHandle_t;
#elif defined(USE_WINDLL)
        typedef HINSTANCE nativeHandle_t;
#endif
        std::atomic<nativeHandle_t> _handle{nullptr};
        std::atomic<GgapiLifecycleFn *> _lifecycleFn{nullptr};

    public:
        explicit NativePlugin(const scope::UsingContext &context, std::string_view name)
            : AbstractPlugin(context, name) {
        }

        NativePlugin(const NativePlugin &) = delete;
        NativePlugin(NativePlugin &&) noexcept = delete;
        NativePlugin &operator=(const NativePlugin &) = delete;
        NativePlugin &operator=(NativePlugin &&) noexcept = delete;
        ~NativePlugin() override;
        void load(const std::filesystem::path &path);
        bool callNativeLifecycle(
            const data::ObjHandle &pluginHandle,
            const data::Symbol &event,
            const data::ObjHandle &dataHandle) override;
        bool isActive() noexcept override;
    };

    /**
     * Loader is responsible for handling all plugins
     */
    class PluginLoader : protected scope::UsesContext {
    private:
        std::shared_ptr<util::NucleusPaths> _paths;
        std::shared_ptr<data::TrackingRoot> _root;
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
         * Plugin component to ERROR_STOP.  This is like STOP but upon completion, NUCLEUS
         * will place the plugin in the BROKEN state.
         */
        data::SymbolInit ERROR_STOP{"error_stop"};
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

        data::SymbolInit SERVICES{"services"};
        data::SymbolInit SYSTEM{"system"};
        data::SymbolInit CONFIGURATION{"configuration"};
        data::SymbolInit LOGGING{"logging"};

        explicit PluginLoader(const scope::UsingContext &context)
            : scope::UsesContext(context), _root(std::make_shared<data::TrackingRoot>(context)) {
            data::SymbolInit::init(
                context,
                {
                    &INITIALIZE,
                    &START,
                    &STOP,
                    &ERROR_STOP,
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

        std::shared_ptr<data::TrackingRoot> root() const {
            return _root;
        }

        std::shared_ptr<config::Topics> getServiceTopics(AbstractPlugin &plugin) const;
        std::shared_ptr<data::StructModelBase> buildParams(
            plugins::AbstractPlugin &plugin, bool partial = false) const;

        void discoverPlugins(const std::filesystem::path &pluginDir);
        void discoverPlugin(const std::filesystem::directory_entry &entry);

        void loadNativePlugin(const std::filesystem::path &path);

        void forAllPlugins(const std::function<void(
                               plugins::AbstractPlugin &,
                               const std::shared_ptr<data::StructModelBase> &)> &fn) const;

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
