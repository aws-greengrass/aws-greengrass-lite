#pragma once
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

namespace config {
    class Manager;
}

namespace errors {
    class Error;
}

namespace plugins {
    class PluginLoader;
}

namespace pubsub {
    class PubSubManager;
}

namespace tasks {
    class TaskThread;
}

namespace scope {
    class Context;
    class ContextGlob;
    class ThreadContextContainer;
    class PerThreadContext;
    class NucleusCallScopeContext;
    class CallScope;

    /**
     * Track a call scope managed by Nucleus - this provides call framing for handle roots
     * that are considered more authoritative than CallScope's.
     */
    class NucleusCallScopeContext : public util::RefObject<NucleusCallScopeContext> {
    public:
        explicit NucleusCallScopeContext(const std::shared_ptr<PerThreadContext> &thread);
        NucleusCallScopeContext(const NucleusCallScopeContext &) = delete;
        NucleusCallScopeContext(NucleusCallScopeContext &&) = delete;
        NucleusCallScopeContext &operator=(const NucleusCallScopeContext &) = delete;
        NucleusCallScopeContext &operator=(NucleusCallScopeContext &&) = delete;
        ~NucleusCallScopeContext();

        std::shared_ptr<NucleusCallScopeContext> set();
        std::shared_ptr<CallScope> getCallScope();
        std::shared_ptr<CallScope> setCallScope(const std::shared_ptr<CallScope> &callScope);
        std::shared_ptr<data::TrackingRoot> root();
        std::shared_ptr<Context> context();

        template<typename T>
        static data::ObjectAnchor make();

        static data::ObjectAnchor anchor(const std::shared_ptr<data::TrackedObject> &obj);
        static data::ObjHandle handle(const std::shared_ptr<data::TrackedObject> &obj) {
            return anchor(obj).getHandle();
        }
        static uint32_t intHandle(const std::shared_ptr<data::TrackedObject> &obj) {
            return handle(obj).asInt();
        }

    private:
        std::shared_ptr<PerThreadContext> _threadContext;
        std::shared_ptr<CallScope> _callScope;
        std::shared_ptr<data::TrackingRoot> _scopeRoot;
    };

    /**
     * Utility class for managing stack-local scope, on Nucleus. Similar functionality to
     * GGAPI CallScope.
     */
    class StackScope {
        std::shared_ptr<NucleusCallScopeContext> _saved;
        std::shared_ptr<NucleusCallScopeContext> _temp;

    public:
        StackScope();
        StackScope(const StackScope &) = default;
        StackScope(StackScope &&) = default;
        StackScope &operator=(const StackScope &) = default;
        StackScope &operator=(StackScope &&) = default;
        ~StackScope() {
            release();
        }
        void release();

        std::shared_ptr<CallScope> getCallScope() {
            if(!_temp) {
                return {};
            }
            return _temp->getCallScope();
        }
    };

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
                auto ii = _contextMap.emplace(ref, std::move(std::make_unique<Ptr>()));
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
        std::shared_ptr<NucleusCallScopeContext> scoped();
        std::shared_ptr<NucleusCallScopeContext> rootScoped();
        std::shared_ptr<NucleusCallScopeContext> changeScope(
            const std::shared_ptr<NucleusCallScopeContext> &newScope);
        std::shared_ptr<CallScope> newCallScope();
        std::shared_ptr<CallScope> getCallScope() {
            return scoped()->getCallScope();
        }
        std::shared_ptr<CallScope> setCallScope(const std::shared_ptr<CallScope> &callScope) {
            return scoped()->setCallScope(callScope);
        }
        std::shared_ptr<tasks::Task> getActiveTask();
        std::shared_ptr<tasks::Task> setActiveTask(const std::shared_ptr<tasks::Task> &task = {});
        const errors::Error &getThreadErrorDetail() const;
        void setThreadErrorDetail(const errors::Error &error);
        std::shared_ptr<tasks::TaskThread> getThreadTaskData();
        std::shared_ptr<tasks::TaskThread> setThreadTaskData(
            const std::shared_ptr<tasks::TaskThread> &threadTaskData = {});

    private:
        std::shared_ptr<Context> _context;
        std::shared_ptr<NucleusCallScopeContext> _scopedContext;
        std::shared_ptr<NucleusCallScopeContext> _rootScopedContext;
        std::shared_ptr<tasks::TaskThread> _threadTaskData;
        std::shared_ptr<tasks::Task> _activeTask;
        errors::Error _threadErrorDetail{data::Symbol{}, ""}; // Modify only via ThreadErrorManager
    };

    /**
     * Localized context, particularly useful for testing.
     */
    class LocalizedContext {
        std::shared_ptr<PerThreadContext> _saved;
        std::shared_ptr<PerThreadContext> _temp;

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
        ~Context() = default;

        static Context &get() {
            return *getPtr();
        }

        static PerThreadContext &thread() {
            return *PerThreadContext::get();
        }

        static std::shared_ptr<Context> getPtr();
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
        std::unique_ptr<ContextGlob> _glob;
    };

    template<typename T>
    inline data::ObjectAnchor NucleusCallScopeContext::make() {
        static_assert(std::is_base_of_v<data::TrackedObject, T>);
        std::shared_ptr<data::TrackedObject> obj = std::make_shared<T>(Context::getPtr());
        return anchor(obj);
    }

    inline data::ObjectAnchor NucleusCallScopeContext::anchor(
        const std::shared_ptr<data::TrackedObject> &obj) {
        return PerThreadContext::get()->scoped()->root()->anchor(obj);
    }

    inline Context &context() {
        return Context::get();
    }

    inline PerThreadContext &thread() {
        return Context::thread();
    }

} // namespace scope
