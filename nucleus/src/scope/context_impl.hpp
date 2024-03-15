#pragma once
#include "context.hpp"

#include "config/config_manager.hpp"
#include "data/handle_table.hpp"
#include "data/string_table.hpp"
#include "errors/error_base.hpp"
#include "lifecycle/sys_properties.hpp"
#include "scope/fixed_pointer.hpp"
#include "tasks/expire_time.hpp"
#include "tasks/task_manager.hpp"

#include <optional>
#include <shared_mutex>
#include <util.hpp>
#include <utility>

namespace config {
    class Manager;
}

namespace logging {
    class LogManager;
}

namespace plugins {
    class PluginLoader;
    class AbstractPlugin;
} // namespace plugins

namespace pubsub {
    class PubSubManager;
}

namespace scope {
    class Context;
    class LazyContext;
    class ThreadContextContainer;
    class PerThreadContext;

    /**
     * Helper class to support thread local data addressing issues on some compiler/OS mixes.
     * Thread-Local storage has a weird behavior (bug) in the combination of Windows/MingGW/GCC
     * where the memory is lost before destructor is called. This complex workaround allows
     * Storing a pointer per thread efficiently while also having correct delete behavior
     * Note, even std::weak_ptr does not work correctly here. Essentially each thread data is
     * stored via a map, and ThreadContextContainer is a cache.
     */
    // NOLINTNEXTLINE(*-special-member-functions)
    class ThreadContextManager {
        using Ptr = std::shared_ptr<PerThreadContext>;
        std::mutex _globalMutex;
        using Map = std::unordered_map<ThreadContextContainer *, std::unique_ptr<Ptr>>;
        Map _contextMap;
        std::atomic_bool _shutdown{false};
        ThreadContextManager() = default;

    public:
        ~ThreadContextManager() {
            _shutdown = true;
        }
        static ThreadContextManager &get() {
            static ThreadContextManager _singleton;
            return _singleton;
        }

        scope::FixedPtr<Ptr> insert(ThreadContextContainer *ref) {
            if(_shutdown) {
                // Unexpected call after destructor
                return {};
            }
            std::unique_lock guard{_globalMutex};
            auto i = _contextMap.find(ref);
            if(i == _contextMap.end()) {
                auto ii = _contextMap.emplace(ref, std::make_unique<Ptr>());
                if(!ii.second) {
                    throw std::runtime_error("Failed to create thread-context data");
                }
                i = ii.first;
            }
            return scope::FixedPtr<Ptr>::of(i->second.get());
        }

        std::unique_ptr<Ptr> remove(ThreadContextContainer *ref) {
            if(_shutdown) {
                // Called after destructor - just ignore
                return {};
            }
            std::unique_lock guard{_globalMutex};
            auto i = _contextMap.find(ref);
            if(i == _contextMap.end()) {
                return {};
            }
            std::unique_ptr<Ptr> removed = std::move(i->second);
            _contextMap.erase(i);
            return removed;
        }
    };

    /**
     * Helper class to support thread local data addressing issues on some compiler/OS mixes.
     * Used with ThreadContextManager.
     */
    class ThreadContextContainer {
        using Ptr = std::shared_ptr<PerThreadContext>;
        scope::FixedPtr<Ptr> _cachedIndirect;

    public:
        ThreadContextContainer() {
            _cachedIndirect = ThreadContextManager::get().insert(this);
        }
        ~ThreadContextContainer() {
            // Consider _context to be invalid
            // removed will provide a valid context
            auto removed = ThreadContextManager::get().remove(this);
            removed.reset();
        }
        ThreadContextContainer(const ThreadContextContainer &) = delete;
        ThreadContextContainer(ThreadContextContainer &&) = delete;
        ThreadContextContainer &operator=(const ThreadContextContainer &) = delete;
        ThreadContextContainer &operator=(ThreadContextContainer &&) = delete;

        static ThreadContextContainer &perThread() {
            static thread_local ThreadContextContainer _current;
            return _current;
        }

        std::shared_ptr<PerThreadContext> get() {
            return *_cachedIndirect;
        }

        std::shared_ptr<PerThreadContext> set(const std::shared_ptr<PerThreadContext> &context) {
            auto prev = get();
            (*_cachedIndirect) = context;
            return prev;
        }
    };

    /**
     * Per-thread context such as call scopes and also context overrides.
     */
    class PerThreadContext : public util::RefObject<PerThreadContext> {
    public:
        using ModulePair = std::pair<
            std::shared_ptr<plugins::AbstractPlugin>,
            std::shared_ptr<plugins::AbstractPlugin>>;
        PerThreadContext() = default;
        PerThreadContext(const PerThreadContext &) = delete;
        PerThreadContext(PerThreadContext &&) = delete;
        PerThreadContext &operator=(const PerThreadContext &) = delete;
        PerThreadContext &operator=(PerThreadContext &&) = delete;
        ~PerThreadContext() = default;

        std::shared_ptr<PerThreadContext> set();
        static std::shared_ptr<PerThreadContext> get();
        static std::shared_ptr<PerThreadContext> reset();
        std::shared_ptr<Context> context();
        std::shared_ptr<Context> changeContext(const std::shared_ptr<Context> &newContext);
        const errors::Error &getThreadErrorDetail() const;
        void setThreadErrorDetail(const errors::Error &error);

        std::shared_ptr<data::RootHandle> setTempRoot(
            const std::shared_ptr<data::RootHandle> &root) noexcept;
        [[nodiscard]] std::shared_ptr<data::RootHandle> getTempRoot() const noexcept;
        ModulePair setModules(const ModulePair &modules);
        std::shared_ptr<plugins::AbstractPlugin> getEffectiveModule();
        std::shared_ptr<plugins::AbstractPlugin> setEffectiveModule(
            const std::shared_ptr<plugins::AbstractPlugin> &newModule);
        std::shared_ptr<plugins::AbstractPlugin> getParentModule();

    private:
        // Values are maintained per-thread, no locking required
        std::shared_ptr<Context> _context;
        std::shared_ptr<data::RootHandle> _tempRoot;
        std::shared_ptr<plugins::AbstractPlugin> _parentModule;
        std::shared_ptr<plugins::AbstractPlugin> _effectiveModule;
        errors::Error _threadErrorDetail{data::Symbol{}, ""}; // Modify only via ThreadErrorManager
    };

    /**
     * Creates a temporary root on the thread that can be used with the ggapiMakeTemp call
     */
    class TempRoot {
        std::shared_ptr<data::RootHandle> _prev;
        std::shared_ptr<data::RootHandle> _temp;

    public:
        TempRoot();
        TempRoot(const TempRoot &) = delete;
        TempRoot(TempRoot &&) = delete;
        TempRoot &operator=(const TempRoot &) = delete;
        TempRoot &operator=(TempRoot &&) = delete;
        ~TempRoot() noexcept;
        data::RootHandle &root() noexcept;
    };

    /**
     * Localized context, used for testing to localize testing/context information.
     */
    class LocalizedContext {
        std::shared_ptr<PerThreadContext> _saved;
        std::shared_ptr<PerThreadContext> _temp;
        bool _applyTerminate{false};

    public:
        LocalizedContext();
        explicit LocalizedContext(const std::shared_ptr<Context> &context);
        LocalizedContext(const LocalizedContext &) = default;
        LocalizedContext(LocalizedContext &&) = default;
        LocalizedContext &operator=(const LocalizedContext &) = default;
        LocalizedContext &operator=(LocalizedContext &&) = default;
        ~LocalizedContext();

        std::shared_ptr<PerThreadContext> context() {
            return _temp;
        }
    };

    /**
     * GG-Interop: Context name matches similar functionality in GG-Java
     * This context provides access to global tables and config. Note that when testing, it
     * is possible to localize the context to aid test scenarios.
     */
    class Context : public util::RefObject<Context> {

    public:
        static std::shared_ptr<Context> create();
        Context() = default;
        Context(const Context &) = delete;
        Context(Context &&) = delete;
        Context &operator=(const Context &) = delete;
        Context &operator=(Context &&) = delete;
        virtual ~Context();
        void terminate();

        static std::shared_ptr<Context> get();
        static std::shared_ptr<Context> getDefaultContext();

        data::SymbolTable &symbols() {
            return _stringTable;
        }
        data::HandleTable &handles() {
            return _handleTable;
        }
        config::Manager &configManager();
        tasks::TaskManager &taskManager();
        pubsub::PubSubManager &lpcTopics();
        plugins::PluginLoader &pluginLoader();
        logging::LogManager &logManager();
        std::mutex &cycleCheckMutex() {
            return _cycleCheckMutex;
        }
        lifecycle::SysProperties &sysProperties() {
            return _sysProperties;
        }

        data::Symbol symbolFromInt(uint32_t s);
        data::ObjHandle handleFromInt(uint32_t h);
        data::Symbol intern(std::string_view str);
        template<typename T = data::TrackedObject>
        std::shared_ptr<T> objFromInt(uint32_t h) {
            return handleFromInt(h).toObject<T>();
        }

    private:
        data::HandleTable _handleTable;
        data::SymbolTable _stringTable;
        lifecycle::SysProperties _sysProperties;
        std::mutex _cycleCheckMutex;
        std::unique_ptr<LazyContext> _lazyContext;
        std::once_flag _lazyInitFlag;

        LazyContext &lazy() {
            std::call_once(_lazyInitFlag, &Context::lazyInit, this);
            return *_lazyContext;
        }
        void lazyInit();
    };

} // namespace scope
