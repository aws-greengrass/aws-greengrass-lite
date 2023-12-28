#pragma once
#include "scope/context_full.hpp"

namespace scope {

    /**
     * Contextual information that can only be initialized AFTER Context has been initialized. For
     * example, if they need a shared pointer to Context, or if they contain symbols. Safety is
     * achieved by going through lazy() helper function, which can only be called once context has
     * been initialized.
     */
    class LazyContext {
        friend class Context;

    private:
        std::weak_ptr<Context> _context;

    public:
        explicit LazyContext(const std::shared_ptr<Context> &context)
            : _context(context), _configManager(context), _taskManager(context),
              _lpcTopics(context), _loader(context),
              _logManager(std::make_shared<logging::LogManager>(context)) {
        }
        ~LazyContext();
        void terminate();

    private:
        config::Manager _configManager;
        tasks::TaskManager _taskManager;
        pubsub::PubSubManager _lpcTopics;
        plugins::PluginLoader _loader;
        std::shared_ptr<logging::LogManager> _logManager;
    };

} // namespace scope
