#include "context.hpp"
#include "scope/context_full.hpp"
#include "scope/context_glob.hpp"
#include <cpp_api.hpp>

const auto LOG = // NOLINT(cert-err58-cpp)
    logging::Logger::of("com.aws.greengrass.scope.Context");

namespace scope {

    std::shared_ptr<PerThreadContext> PerThreadContext::get() {
        auto tc = ThreadContextContainer::perThread().get();
        if(!tc) {
            tc = std::make_shared<PerThreadContext>();
            ThreadContextContainer::perThread().set(tc);
        }
        return tc;
    }

    std::shared_ptr<PerThreadContext> PerThreadContext::set() {
        return ThreadContextContainer::perThread().set(baseRef());
    }

    std::shared_ptr<PerThreadContext> PerThreadContext::reset() {
        return ThreadContextContainer::perThread().set({});
    }

    StackScope::StackScope() {
        auto thread = PerThreadContext::get();
        auto newScope = std::make_shared<NucleusCallScopeContext>(thread);
        _saved = newScope->set();
        _temp = newScope;
    }

    void StackScope::release() {
        if(_temp) {
            _saved->set();
            _temp = nullptr;
        }
    }

    LocalizedContext::LocalizedContext() {
        auto newScope = std::make_shared<PerThreadContext>();
        _saved = newScope->set();
        _temp = newScope;
    }

    LocalizedContext::LocalizedContext(const std::shared_ptr<Context> &context)
        : LocalizedContext() {
        _temp->changeContext(context);
    }

    LocalizedContext::~LocalizedContext() {
        if(_saved) {
            _saved->set();
        } else {
            scope::PerThreadContext::reset();
        }
    }

    NucleusCallScopeContext::NucleusCallScopeContext(
        const std::shared_ptr<PerThreadContext> &thread)
        : _threadContext(thread) {
    }

    std::shared_ptr<NucleusCallScopeContext> NucleusCallScopeContext::set() {
        auto perThread = _threadContext.lock();
        if(!perThread) {
            // Should never happen, possible during shutdown
            return {};
        }
        return perThread->changeScope(baseRef());
    }

    std::shared_ptr<Context> Context::create() {
        return std::make_shared<Context>();
    }

    void Context::lazyInit() {
        std::shared_ptr<Context> self = baseRef();
        if(!self) {
            throw std::logic_error{"Init cycle: lazy() was called before Context() was created"};
        }
        _lazyContext = std::make_unique<LazyContext>(self);
    }

    std::shared_ptr<Context> Context::getDefaultContext() {
        static std::shared_ptr<Context> deflt{create()};
        return deflt;
    }

    std::shared_ptr<Context> Context::getPtr() {
        std::shared_ptr<PerThreadContext> threadContext = PerThreadContext::get();
        if(threadContext) {
            return threadContext->context();
        } else {
            return getDefaultContext();
        }
    }

    data::Symbol Context::symbolFromInt(uint32_t s) {
        return symbols().apply(data::Symbol::Partial(s));
    }

    data::ObjHandle Context::handleFromInt(uint32_t h) {
        return handles().apply(data::ObjHandle::Partial(h));
    }

    data::Symbol Context::intern(std::string_view str) {
        return symbols().intern(str);
    }
    config::Manager &Context::configManager() {
        return lazy()._configManager;
    }
    tasks::TaskManager &Context::taskManager() {
        return lazy()._taskManager;
    }
    pubsub::PubSubManager &Context::lpcTopics() {
        return lazy()._lpcTopics;
    }
    plugins::PluginLoader &Context::pluginLoader() {
        return lazy()._loader;
    }
    logging::LogManager &Context::logManager() {
        return *lazy()._logManager;
    }

    std::shared_ptr<Context> PerThreadContext::context() {
        // Thread safe - assume object per thread
        if(!_context) {
            _context = Context::getDefaultContext();
        }
        return _context;
    }

    std::shared_ptr<NucleusCallScopeContext> PerThreadContext::rootScoped() {
        // Only one per thread
        auto active = _rootScopedContext;
        if(!active) {
            _rootScopedContext = active = std::make_shared<NucleusCallScopeContext>(baseRef());
        }
        return active;
    }

    std::shared_ptr<Context> PerThreadContext::changeContext(
        // For testing
        const std::shared_ptr<Context> &newContext) {
        auto prev = context();
        _context = newContext;
        return prev;
    }

    std::shared_ptr<NucleusCallScopeContext> PerThreadContext::changeScope(
        const std::shared_ptr<NucleusCallScopeContext> &context) {
        auto prev = scoped();
        _scopedContext = context;
        return prev;
    }

    std::shared_ptr<NucleusCallScopeContext> PerThreadContext::scoped() {
        // Either explicit, or the per-thread scope
        auto active = _scopedContext;
        if(!active) {
            _scopedContext = active = rootScoped();
        }
        return active;
    }

    std::shared_ptr<Context> NucleusCallScopeContext::context() {
        auto perThread = _threadContext.lock();
        if(!perThread) {
            // Should never happen, possible during shutdown
            return {};
        }
        return perThread->context();
    }

    std::shared_ptr<data::TrackingRoot> NucleusCallScopeContext::root() {
        auto active = _scopeRoot;
        if(!active) {
            _scopeRoot = active = std::make_shared<data::TrackingRoot>(context());
        }
        return active;
    }

    std::shared_ptr<CallScope> NucleusCallScopeContext::getCallScope() {
        // Thread safe - assume object per thread
        auto active = _callScope;
        if(!active) {
            if(!_scopeRoot) {
                _scopeRoot = std::make_shared<data::TrackingRoot>(context());
            }
            _callScope = active = CallScope::create(context(), _scopeRoot, baseRef(), {});
            errors::ThreadErrorContainer::get().reset();
        }
        return active;
    }

    std::shared_ptr<CallScope> PerThreadContext::newCallScope() {
        auto prev = getCallScope();
        return CallScope::create(_context, prev->root(), scoped(), prev);
    }

    std::shared_ptr<CallScope> NucleusCallScopeContext::setCallScope(
        const std::shared_ptr<CallScope> &callScope) {
        std::shared_ptr<CallScope> prev = getCallScope();
        _callScope = callScope;
        errors::ThreadErrorContainer::get().reset();
        return prev;
    }

    /**
     * Used only from errors::ThreadErrorContainer - makes a copy of current thread error.
     */
    void PerThreadContext::setThreadErrorDetail(const errors::Error &error) {
        _threadErrorDetail = error;
    }

    /**
     * Retrieve current thread error by reference (ensures the what() value does not go away).
     */
    const errors::Error &PerThreadContext::getThreadErrorDetail() const {
        return _threadErrorDetail;
    }

    /**
     * Retrieve a per-thread object that is used for task strategy, data and affinity.
     */
    std::shared_ptr<tasks::TaskThread> PerThreadContext::getThreadTaskData() {
        auto active = _threadTaskData;
        if(!active) {
            // Auto-assign a thread context
            _threadTaskData = active = std::make_shared<tasks::FixedTaskThread>(context());
        }
        return active;
    }

    /**
     * Change task strategy and per thread task data associated with thread.
     */
    std::shared_ptr<tasks::TaskThread> PerThreadContext::setThreadTaskData(
        const std::shared_ptr<tasks::TaskThread> &threadTaskData) {
        auto prev = _threadTaskData;
        _threadTaskData = threadTaskData;
        return prev;
    }

    std::shared_ptr<tasks::Task> PerThreadContext::getActiveTask() {
        std::shared_ptr<tasks::Task> active = _activeTask;
        if(!active) {
            // Auto-assign a default task, anchored to local context
            active = std::make_shared<tasks::Task>(context());
            auto anchor = rootScoped()->root()->anchor(active);
            active->setSelf(anchor.getHandle());
            _activeTask = active;
        }
        return active;
    }

    std::shared_ptr<tasks::Task> PerThreadContext::setActiveTask(
        const std::shared_ptr<tasks::Task> &task) {
        auto prev = _activeTask;
        _activeTask = task;
        return prev;
    }

    /**
     * Used when saving/restoring module state, pair is (parent,effective), which are both
     * the same when setting, but may be different when restoring.
     */
    PerThreadContext::ModulePair PerThreadContext::setModules(const ModulePair &modules) {
        auto prev = std::pair(_parentModule, _effectiveModule);
        _parentModule = modules.first;
        _effectiveModule = modules.second;
        return prev;
    }

    /**
     * Called by Nucleus to retrieve the 'owning' parent module, or nullptr when running
     * in Nucleus context
     */
    std::shared_ptr<plugins::AbstractPlugin> PerThreadContext::getParentModule() {
        return _parentModule;
    }

    /**
     * Called by Nucleus to retrieve the context module - typically this will be same
     * as parent.
     */
    std::shared_ptr<plugins::AbstractPlugin> PerThreadContext::getEffectiveModule() {
        return _effectiveModule;
    }

    /**
     * Called by A plugin to change context - this may be a direct child of current module, or
     * a direct child of parent, or reset to parent
     */
    std::shared_ptr<plugins::AbstractPlugin> PerThreadContext::setEffectiveModule(
        const std::shared_ptr<plugins::AbstractPlugin> &newModule) {
        std::shared_ptr<plugins::AbstractPlugin> prevMod = _effectiveModule;
        if(!_parentModule) {
            // If no parent, then this is open-ended (e.g. testing)
            _effectiveModule = newModule;
            return prevMod;
        }
        if(!newModule || newModule == _parentModule) {
            // Action to reset to parent
            _effectiveModule = _parentModule;
            return prevMod;
        }
        auto asDelegate = std::dynamic_pointer_cast<plugins::DelegatePlugin>(newModule);
        if(asDelegate) {
            auto newParent = asDelegate->getParent();
            if(newParent == _parentModule || newParent == prevMod) {
                // Action to change which child is used
                _effectiveModule = newModule;
                return prevMod;
            }
        }
        LOG.atError()
            .event("changeModule")
            .logAndThrow(
                errors::ModuleError{"Not permitted to change context to specified module"});
    }

    NucleusCallScopeContext::~NucleusCallScopeContext() {
        errors::ThreadErrorContainer::get().reset();
    }

    Context &SharedContextMapper::context() const {
        std::shared_ptr<Context> context = _context.lock();
        if(!context) {
            throw std::runtime_error("Using Context after it is deleted");
        }
        return *context;
    }

    data::Symbol::Partial SharedContextMapper::partial(const data::Symbol &symbol) const {
        return context().symbols().partial(symbol);
    }

    data::Symbol SharedContextMapper::apply(data::Symbol::Partial partial) const {
        return context().symbols().apply(partial);
    }

} // namespace scope
