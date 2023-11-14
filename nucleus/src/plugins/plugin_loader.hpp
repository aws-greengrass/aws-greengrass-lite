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

namespace plugins {
    //
    // Abstract plugins also acts as a global anchor for the given plugins module
    //
    class AbstractPlugin : public data::TrackingScope {
    protected:
        std::string _moduleName;

    public:
        explicit AbstractPlugin(
            const std::shared_ptr<scope::Context> &context, const std::string_view &name)
            : TrackingScope(context), _moduleName{name} {
        }

        virtual bool lifecycle(
            data::ObjHandle pluginRoot,
            data::Symbol phase,
            const std::shared_ptr<data::StructModelBase> &data) = 0;

        virtual bool isActive() noexcept {
            return true;
        }

        [[nodiscard]] std::string getName() {
            return _moduleName;
        }
    };

    //
    // Delegate plugins is managed by a parent (typically native) plugins
    //
    class DelegatePlugin : public AbstractPlugin {
    private:
        mutable std::shared_mutex _mutex;
        std::shared_ptr<AbstractPlugin> _parent; // delegate keeps parent in memory
        ggapiLifecycleCallback _delegateLifecycle{nullptr}; // called to handle
                                                            // this delegate
        uintptr_t _delegateContext{0}; // use of this defined by delegate

    public:
        explicit DelegatePlugin(
            const std::shared_ptr<scope::Context> &context,
            std::string_view name,
            const std::shared_ptr<AbstractPlugin> &parent,
            ggapiLifecycleCallback delegateLifecycle,
            uintptr_t delegateContext)
            : AbstractPlugin(context, name), _parent{parent}, _delegateLifecycle{delegateLifecycle},
              _delegateContext{delegateContext} {
        }

        bool lifecycle(
            data::ObjHandle pluginRoot,
            data::Symbol phase,
            const std::shared_ptr<data::StructModelBase> &data) override;
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
        void load(const std::string &filePath);
        bool lifecycle(
            data::ObjHandle pluginRoot,
            data::Symbol phase,
            const std::shared_ptr<data::StructModelBase> &data) override;
        bool isActive() noexcept override;
    };

    //
    // Loader is responsible for handling all plugins
    //
    class PluginLoader {
    private:
        std::weak_ptr<scope::Context> _context;
        std::shared_ptr<data::TrackingRoot> _root;
        data::SymbolInit _bootstrap{"bootstrap"};
        data::SymbolInit _discover{"discover"};
        data::SymbolInit _start{"start"};
        data::SymbolInit _run{"run"};
        data::SymbolInit _terminate{"terminate"};

    public:
        explicit PluginLoader(const std::shared_ptr<scope::Context> &context)
            : _context(context), _root(std::make_shared<data::TrackingRoot>(context)) {
            data::SymbolInit::init(context, {&_bootstrap, &_discover, &_start, &_run, &_terminate});
        }

        scope::Context &context() const {
            return *_context.lock();
        }

        void discoverPlugins(const std::filesystem::path &pluginDir);
        void discoverPlugin(const std::filesystem::directory_entry &entry);

        void loadNativePlugin(const std::string &name);

        void lifecycleBootstrap(const std::shared_ptr<data::StructModelBase> &data);
        void lifecycleDiscover(const std::shared_ptr<data::StructModelBase> &data);
        void lifecycleStart(const std::shared_ptr<data::StructModelBase> &data);
        void lifecycleRun(const std::shared_ptr<data::StructModelBase> &data);
        void lifecycleTerminate(const std::shared_ptr<data::StructModelBase> &data);
        void lifecycle(data::Symbol phase, const std::shared_ptr<data::StructModelBase> &data);
    };
} // namespace plugins
