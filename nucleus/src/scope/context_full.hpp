#pragma once
#include "config/config_manager.hpp"
#include "config/config_nodes.hpp"
#include "lifecycle/sys_properties.hpp"
#include "logging/log_manager.hpp"
#include "plugins/plugin_loader.hpp"
#include "pubsub/local_topics.hpp"
#include "scope/call_scope.hpp"
#include "scope/context_impl.hpp"
#include "tasks/task_async.hpp"
#include "tasks/task_manager.hpp"
#include "tasks/task_threads.hpp"

namespace scope {
    inline ContextRef context() {
        return Context::get();
    }

    inline PerThreadContextRef thread() {
        return PerThreadContext::get();
    }

} // namespace scope
