#pragma once
#include "data/environment.hpp"
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

namespace plugins {
    //
    // Abstract plugins also acts as a global anchor for the given plugins module
    //
    class AbstractPlugin : public data::TrackingScope {
    protected:
        std::string _moduleName{};

    public:
        explicit AbstractPlugin(data::Environment &environment, const std::string_view &name)
            : TrackingScope(environment), _moduleName{name} {
        }

        virtual bool lifecycle(
            data::ObjHandle pluginRoot,
            data::StringOrd phase,
            const std::shared_ptr<data::StructModelBase> &data
        ) = 0;

        virtual bool isActive() noexcept {
            return true;
        }
    };

    //
    // Delegate plugins is managed by a parent (typically native) plugins
    //
    class DelegatePlugin : public AbstractPlugin {
    private:
        std::shared_ptr<AbstractPlugin> _parent; // delegate keeps parent in memory
        ggapiLifecycleCallback _delegateLifecycle{nullptr}; // called to handle
                                                            // this delegate
        uintptr_t _delegateContext{0}; // use of this defined by delegate

    public:
        explicit DelegatePlugin(
            data::Environment &environment,
            std::string_view name,
            const std::shared_ptr<AbstractPlugin> &parent,
            ggapiLifecycleCallback delegateLifecycle,
            uintptr_t delegateContext
        )
            : AbstractPlugin(environment, name), _parent{parent},
              _delegateLifecycle{delegateLifecycle}, _delegateContext{delegateContext} {
        }

        bool lifecycle(
            data::ObjHandle pluginRoot,
            data::StringOrd phase,
            const std::shared_ptr<data::StructModelBase> &data
        ) override;
    };

    //
    // Native plugins are first-class, handled by the Nucleus itself
    //
    class NativePlugin : public AbstractPlugin {
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
        explicit NativePlugin(data::Environment &environment, std::string_view name)
            : AbstractPlugin(environment, name) {
        }

        NativePlugin(const NativePlugin &) = delete;
        NativePlugin(NativePlugin &&) noexcept = delete;
        NativePlugin &operator=(const NativePlugin &) = delete;
        NativePlugin &operator=(NativePlugin &&) noexcept = delete;
        ~NativePlugin() override;
        void load(const std::string &filePath);
        bool lifecycle(
            data::ObjHandle pluginRoot,
            data::StringOrd phase,
            const std::shared_ptr<data::StructModelBase> &data
        ) override;
        bool isActive() noexcept override;
    };

    //
    // Loader is responsible for handling all plugins
    //
    class PluginLoader : public data::TrackingScope {
    public:
        explicit PluginLoader(data::Environment &environment) : TrackingScope(environment) {
        }

        void discoverPlugins(const std::filesystem::path &pluginDir);
      static   void discoverPlugin(const std::filesystem::directory_entry &entry);

        void loadNativePlugin(const std::string &name);

        void lifecycleBootstrap(const std::shared_ptr<data::StructModelBase> &data);
        void lifecycleDiscover(const std::shared_ptr<data::StructModelBase> &data);
        void lifecycleStart(const std::shared_ptr<data::StructModelBase> &data);
        void lifecycleRun(const std::shared_ptr<data::StructModelBase> &data);
        void lifecycleTerminate(const std::shared_ptr<data::StructModelBase> &data);
        void lifecycle(data::StringOrd phase, const std::shared_ptr<data::StructModelBase> &data);
    };
} // namespace plugins
