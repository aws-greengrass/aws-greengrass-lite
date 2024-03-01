#pragma once
#include "config/config_manager.hpp"
#include "config/config_nodes.hpp"
#include "lifecycle/sys_properties.hpp"
#include "logging/log_manager.hpp"
#include "plugins/plugin_loader.hpp"
#include "pubsub/local_topics.hpp"
#include "pubsub/promise.hpp"
#include "scope/context_impl.hpp"
#include "tasks/task_manager.hpp"
#include "tasks/task_threads.hpp"

namespace scope {
    inline ContextRef context() {
        return Context::get();
    }

    inline PerThreadContextRef thread() {
        return PerThreadContext::get();
    }

    /**
     * Wraps around std::make_shared with assumption that the type is a subclass of
     * data::TrackedObject and requires context as first parameter.
     */
    template<typename T, typename... Args>
    inline std::shared_ptr<T> makeObject(Args &...args) {
        static_assert(std::is_base_of_v<data::TrackedObject, T>);
        return std::make_shared<T>(context(), std::forward<Args>(args)...);
    }

    /**
     * Given a Nucleus shared object, create a handle to it that is bound to the "effective" module
     * of the thread. This makes the assumption that every thread has a module. Most likely error is
     * described in the exception which is forgetting to call ggapi::ModuleScope::setActive() or
     * equivalent.
     * @param Object to create handle for, may be unset
     * @return Handle, or null-handle if Object was unset
     */
    inline data::ObjHandle asHandle(const std::shared_ptr<data::TrackedObject> &obj) {
        if(obj) {
            // Only look up mod if we need to. This particularly matters when setting active module
            auto mod = thread()->getEffectiveModule();
            if(!mod) {
                throw std::runtime_error("No module context - forgot to call setModule() ?");
            }
            return context()->handles().create(obj, mod->root());
        } else {
            return {};
        }
    }

    /**
     * Create an integer handle for a shared object, otherwise as scope::asHandle.
     *
     * @param Object to create handle for, may be unset
     * @return Integer Handle, or 0 if Object was unset
     */
    inline ggapiObjHandle asIntHandle(const std::shared_ptr<data::TrackedObject> &obj) {
        auto o = asHandle(obj);
        if(o) {
            return o.asInt();
        } else {
            return 0;
        }
    }

} // namespace scope
