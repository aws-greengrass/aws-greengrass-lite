#pragma once
#include "scope/context_full.hpp"

namespace scope {

    /**
     * Context classes that recursively need std::shared_ptr<Context>
     */
    class ContextGlob {
        friend class Context;

    private:
        std::weak_ptr<Context> _context;

    public:
        explicit ContextGlob(const std::shared_ptr<Context> &context)
            : _context(context), _configManager(context), _taskManager(context),
              _lpcTopics(context), _loader(context) {
        }

    private:
        config::Manager _configManager;
        tasks::TaskManager _taskManager;
        pubsub::PubSubManager _lpcTopics;
        plugins::PluginLoader _loader;
    };

} // namespace scope
