#pragma once
#include "data/handle_table.hpp"
#include "data/safe_handle.hpp"
#include "data/shared_struct.hpp"
#include <atomic>
#include <cpp_api.hpp>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>

#if defined(USE_DLFCN)
#include <dlfcn.h>
#elif defined(USE_WINDLL)
#include <windows.h>
#endif

namespace deployment {
    class DeviceConfiguration;
}

namespace tasks {
    class Callback;
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

        explicit AbstractPlugin(
            const std::shared_ptr<scope::Context> &context, const std::string_view &name)
            : TrackingScope(context), _moduleName(name) {
        }

        virtual bool callNativeLifecycle(
            const data::ObjHandle &pluginRoot,
            const data::Symbol &phase,
            const data::ObjHandle &data) = 0;

        void lifecycle(data::Symbol phase, const std::shared_ptr<data::StructModelBase> &data);

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

    //
    // Delegate plugins are managed by a parent (typically native) plugins
    //
    class DelegatePlugin : public AbstractPlugin {
    private:
        mutable std::shared_mutex _mutex;
        std::shared_ptr<AbstractPlugin> _parent; // delegate keeps parent in memory
        std::shared_ptr<tasks::Callback> _callback;

    public:
        explicit DelegatePlugin(
            const std::shared_ptr<scope::Context> &context,
            std::string_view name,
            const std::shared_ptr<AbstractPlugin> &parent,
            const std::shared_ptr<tasks::Callback> &callback)
            : AbstractPlugin(context, name), _parent{parent}, _callback{callback} {
        }

        bool callNativeLifecycle(
            const data::ObjHandle &pluginRoot,
            const data::Symbol &phase,
            const data::ObjHandle &data) override;
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
        typedef bool (*lifecycleFn_t)(uint32_t moduleHandle, uint32_t phase, uint32_t data);
#elif defined(USE_WINDLL)
        typedef HINSTANCE nativeHandle_t;
        typedef bool(WINAPI *lifecycleFn_t)(uint32_t globalHandle, uint32_t phase, uint32_t data);
#endif
        std::atomic<nativeHandle_t> _handle{nullptr};
        std::atomic<lifecycleFn_t> _lifecycleFn{nullptr};

    public:
        explicit NativePlugin(const std::shared_ptr<scope::Context> &context, std::string_view name)
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
            const data::Symbol &phase,
            const data::ObjHandle &dataHandle) override;
        bool isActive() noexcept override;
    };

    /**
     * Loader is responsible for handling all plugins
     */
    class PluginLoader {
    private:
        std::weak_ptr<scope::Context> _context;
        std::shared_ptr<data::TrackingRoot> _root;
        std::shared_ptr<deployment::DeviceConfiguration> _deviceConfig;

    public:
        /*
         * Plugin loaded, recipe/config unknown
         */
        data::SymbolInit BOOTSTRAP{"bootstrap"};
        /*
         * Plugin loaded, recipe/config filled in
         */
        data::SymbolInit BIND{"bind"};
        /**
         * Request to discover nested plugins (may be deprecated)
         */
        data::SymbolInit DISCOVER{"discover"};
        /**
         * Plugin component enter start phase - request to get ready for run
         */
        data::SymbolInit START{"start"};
        /**
         * Plugin component enter run phase - plugin can issue traffic to other plugins
         */
        data::SymbolInit RUN{"run"};
        /**
         * Plugin component enter terminate phase - plugin is about to be unloaded
         */
        data::SymbolInit TERMINATE{"terminate"};
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

        explicit PluginLoader(const std::shared_ptr<scope::Context> &context)
            : _context(context), _root(std::make_shared<data::TrackingRoot>(context)) {
            data::SymbolInit::init(
                context,
                {&BOOTSTRAP,
                 &BIND,
                 &DISCOVER,
                 &START,
                 &RUN,
                 &TERMINATE,
                 &CONFIG_ROOT,
                 &CONFIG,
                 &NUCLEUS_CONFIG,
                 &NAME,
                 &SERVICES,
                 &SYSTEM});
        }

        scope::Context &context() const {
            return *_context.lock();
        }

        std::shared_ptr<data::TrackingRoot> root() const {
            return _root;
        }

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
    };
} // namespace plugins
