#include "config/publish_queue.hpp"
#include "logging/log_queue.hpp"
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

    LocalizedContext::LocalizedContext() {
        auto newScope = std::make_shared<PerThreadContext>();
        _saved = newScope->set();
        _temp = newScope;
    }

    LocalizedContext::LocalizedContext(const std::shared_ptr<Context> &context)
        : LocalizedContext() {
        assert(context.use_count() == 1);
        _temp->changeContext(context);
        _applyTerminate = true; // TODO: Temp Workaround - there is a count leak going on
    }

    LocalizedContext::~LocalizedContext() {
        std::shared_ptr<Context> context = _temp->context();
        if(_saved) {
            _saved->set();
        } else {
            scope::PerThreadContext::reset();
        }
        if(_applyTerminate) {
            context->terminate();
        }
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

    std::shared_ptr<Context> Context::get() {
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
        // TODO: normally this would not be safe, however we know logManager() will survive until
        // context is destroyed. Even so, clean this up at some point
        return *lazy()._logManager.load();
    }
    Context::~Context() {
        terminate();
    }

    void Context::terminate() {
        if(_lazyContext) {
            _lazyContext->terminate();
        }
        _lazyContext.reset();
    }

    std::shared_ptr<Context> PerThreadContext::context() {
        // Thread safe - assume object per thread
        if(!_context) {
            _context = Context::getDefaultContext();
        }
        return _context;
    }

    std::shared_ptr<Context> PerThreadContext::changeContext(
        // For testing
        const std::shared_ptr<Context> &newContext) {
        auto prev = context();
        _context = newContext;
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
     * Used when saving/restoring module state, pair is (parent,effective), which are both
     * the same when setting, but may be different when restoring.
     */
    PerThreadContext::ModulePair PerThreadContext::setModules(const ModulePair &modules) {
        auto prev = std::pair(_parentModule, _effectiveModule);
        _parentModule = modules.first;
        _effectiveModule = modules.second;
        return prev;
    }

    std::shared_ptr<data::RootHandle> PerThreadContext::getTempRoot() const noexcept {
        return _tempRoot;
    }

    std::shared_ptr<data::RootHandle> PerThreadContext::setTempRoot(
        const std::shared_ptr<data::RootHandle> &root) noexcept {
        auto prev = _tempRoot;
        _tempRoot = root;
        return prev;
    }

    std::shared_ptr<data::RootHandle> TempRoot::makeTemp(const ContextRef &context) {
        // NOLINTNEXTLINE(*-make-shared) make_shared causes RootHandle to be destroyed early
        return std::shared_ptr<data::RootHandle>{
            new data::RootHandle(context->handles().createRoot())};
    }

    TempRoot::TempRoot() : TempRoot(scope::context()) {
    }

    TempRoot::TempRoot(const ContextRef &context) : _temp(makeTemp(context)) {
        _prev = thread()->setTempRoot(_temp);
    }

    TempRoot::~TempRoot() noexcept {
        thread()->setTempRoot(_prev);
    }

    data::RootHandle &TempRoot::root() noexcept {
        return *_temp;
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

    data::Symbol::Partial SharedContextMapper::partial(const data::Symbol &symbol) const {
        return context()->symbols().partial(symbol);
    }

    data::Symbol SharedContextMapper::apply(data::Symbol::Partial partial) const {
        return context()->symbols().apply(partial);
    }

    LazyContext::~LazyContext() {
        terminate();
    }

    void LazyContext::terminate() {
        _taskManager.shutdownAndWait();
        _configManager.publishQueue().stop();
        _logManager.load()->publishQueue()->stop();
    }

    UsingContext::UsingContext() noexcept : _context(context()) {
    }

    data::RootHandle UsingContext::newRootHandle() const {
        return context()->handles().createRoot();
    }

} // namespace scope
