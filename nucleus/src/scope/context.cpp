#include "context.hpp"
#include "scope/context_full.hpp"
#include "scope/context_glob.hpp"
#include <cpp_api.hpp>

namespace scope {

    std::shared_ptr<ThreadContext> ThreadContext::get() {
        auto tc = ThreadContextContainer::perThread().get();
        if(!tc) {
            tc = std::make_shared<ThreadContext>();
            ThreadContextContainer::perThread().set(tc);
        }
        return tc;
    }

    std::shared_ptr<ThreadContext> ThreadContext::set() {
        return ThreadContextContainer::perThread().set(baseRef());
    }

    std::shared_ptr<ThreadContext> ThreadContext::reset() {
        return ThreadContextContainer::perThread().set({});
    }

    StackScope::StackScope() {
        auto thread = ThreadContext::get();
        auto newScope = std::make_shared<ScopedContext>(thread);
        _saved = newScope->set();
        _temp = newScope;
    }

    StackScope::~StackScope() {
        _saved->set();
    }

    LocalizedScope::LocalizedScope() {
        auto newScope = std::make_shared<ThreadContext>();
        _saved = newScope->set();
        _temp = newScope;
    }

    LocalizedScope::LocalizedScope(const std::shared_ptr<Context> &context) : LocalizedScope() {
        _temp->changeContext(context);
    }

    LocalizedScope::~LocalizedScope() {
        if(_saved) {
            _saved->set();
        } else {
            scope::ThreadContext::reset();
        }
    }

    ScopedContext::ScopedContext(const std::shared_ptr<ThreadContext> &thread)
        : _threadContext(thread) {
    }

    std::shared_ptr<ScopedContext> ScopedContext::set() {
        return _threadContext->changeScope(baseRef());
    }

    std::shared_ptr<Context> Context::create() {
        std::shared_ptr<Context> impl{std::make_shared<Context>()};
        impl->_glob = std::make_unique<ContextGlob>(impl);
        return impl;
    }

    std::shared_ptr<Context> Context::getDefaultContext() {
        static std::shared_ptr<Context> deflt{create()};
        return deflt;
    }

    std::shared_ptr<Context> Context::getPtr() {
        std::shared_ptr<ThreadContext> threadContext = ThreadContext::get();
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
        return _glob->_configManager;
    }
    tasks::TaskManager &Context::taskManager() {
        return _glob->_taskManager;
    }
    pubsub::PubSubManager &Context::lpcTopics() {
        return _glob->_lpcTopics;
    }
    plugins::PluginLoader &Context::pluginLoader() {
        return _glob->_loader;
    }

    std::shared_ptr<Context> ThreadContext::context() {
        // Thread safe - assume object per thread
        if(!_context) {
            _context = Context::getDefaultContext();
        }
        return _context;
    }

    std::shared_ptr<ScopedContext> ThreadContext::rootScoped() {
        // Only one per thread
        auto active = _rootScopedContext;
        if(!active) {
            _rootScopedContext = active = std::make_shared<ScopedContext>(baseRef());
        }
        return active;
    }

    std::shared_ptr<Context> ThreadContext::changeContext(
        // For testing
        const std::shared_ptr<Context> &newContext) {
        auto prev = context();
        _context = newContext;
        return prev;
    }

    std::shared_ptr<ScopedContext> ThreadContext::changeScope(
        const std::shared_ptr<ScopedContext> &context) {
        auto prev = scoped();
        _scopedContext = context;
        return prev;
    }

    std::shared_ptr<ScopedContext> ThreadContext::scoped() {
        // Either explicit, or the per-thread scope
        auto active = _scopedContext;
        if(!active) {
            _scopedContext = active = rootScoped();
        }
        return active;
    }

    std::shared_ptr<Context> ScopedContext::context() {
        return _threadContext->context();
    }

    std::shared_ptr<data::TrackingRoot> ScopedContext::root() {
        auto active = _scopeRoot;
        if(!active) {
            _scopeRoot = active = std::make_shared<data::TrackingRoot>(context());
        }
        return active;
    }

    std::shared_ptr<CallScope> ScopedContext::getCallScope() {
        // Thread safe - assume object per thread
        auto active = _callScope;
        if(!active) {
            if(!_scopeRoot) {
                _scopeRoot = std::make_shared<data::TrackingRoot>(context());
            }
            _callScope = active = CallScope::create(context(), _scopeRoot);
        }
        return active;
    }

    std::shared_ptr<CallScope> ThreadContext::newCallScope() {
        auto prev = getCallScope();
        return CallScope::create(_context, prev->root());
    }

    std::shared_ptr<CallScope> ScopedContext::setCallScope(
        const std::shared_ptr<CallScope> &callScope) {
        std::shared_ptr<CallScope> prev = getCallScope();
        _callScope = callScope;
        return prev;
    }

    data::Symbol ThreadContext::setLastError(const data::Symbol &errorSymbol) {
        data::Symbol prev = getLastError();
        ::ggapiSetError(errorSymbol.asInt());
        return prev;
    }

    data::Symbol ThreadContext::getLastError() {
        return context()->symbolFromInt(::ggapiGetError());
    }

    std::shared_ptr<tasks::TaskThread> ThreadContext::getThreadContext() {
        auto active = _threadContext;
        if(!active) {
            // Auto-assign a thread context
            _threadContext = active = std::make_shared<tasks::FixedTaskThread>(context());
        }
        return active;
    }

    std::shared_ptr<tasks::TaskThread> ThreadContext::setThreadContext(
        const std::shared_ptr<tasks::TaskThread> &threadContext) {
        auto prev = _threadContext;
        _threadContext = threadContext;
        return prev;
    }

    std::shared_ptr<tasks::Task> ThreadContext::getActiveTask() {
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

    std::shared_ptr<tasks::Task> ThreadContext::setActiveTask(
        const std::shared_ptr<tasks::Task> &task) {
        auto prev = _activeTask;
        _activeTask = task;
        return prev;
    }

    ScopedContext::~ScopedContext() {
        ::ggapiSetError(0);
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
