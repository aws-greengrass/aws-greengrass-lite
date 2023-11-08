#pragma once
#include "cpp_api.hpp"
#include <atomic>

namespace ggapi {

    class Plugin {
    private:
        std::atomic<ModuleScope> _moduleScope{ModuleScope{}};
        std::atomic<StringOrd> _phase{StringOrd{}};

        //
        // Generic lifecycle dispatch
        //
        bool lifecycleDispatch(StringOrd phase, Struct data) {
            if(phase == BOOTSTRAP) {
                return onBootstrap(data);
            } else if(phase == DISCOVER) {
                return onDiscover(data);
            } else if(phase == START) {
                return onStart(data);
            } else if(phase == RUN) {
                return onRun(data);
            } else if(phase == TERMINATE) {
                return onTerminate(data);
            } else {
                // Return to caller that phase was not handled
                return false;
            }
        }

    public:
        static const StringOrd BOOTSTRAP;
        static const StringOrd DISCOVER;
        static const StringOrd START;
        static const StringOrd RUN;
        static const StringOrd TERMINATE;

        Plugin() noexcept = default;
        Plugin(const Plugin &) = delete;
        Plugin(Plugin &&) noexcept = delete;
        Plugin &operator=(const Plugin &) = delete;
        Plugin &operator=(Plugin &&) noexcept = delete;
        virtual ~Plugin() = default;

        bool lifecycle(uint32_t moduleHandle, uint32_t phase, uint32_t data) noexcept {
            // No exceptions may cross API boundary
            // Return true if handled.
            return trapErrorReturn<bool>([this, moduleHandle, phase, data]() {
                return lifecycle(ModuleScope{moduleHandle}, StringOrd{phase}, Struct{data});
            });
        }

        bool lifecycle(ModuleScope moduleScope, StringOrd phase, Struct data) {
            _moduleScope = moduleScope;
            _phase = phase;
            beforeLifecycle(phase, data);
            bool handled = lifecycleDispatch(phase, data);
            afterLifecycle(phase, data);
            return handled;
        }

        //
        // Retrieve scope of plugin
        //
        ModuleScope getScope() {
            return _moduleScope.load();
        }

        //
        // Current phase driven by lifecycle manager
        //
        StringOrd getCurrentPhase() {
            return _phase.load();
        }

        virtual void beforeLifecycle(StringOrd phase, Struct data) {
        }

        virtual void afterLifecycle(StringOrd phase, Struct data) {
        }

        //
        // For plugins discovered during bootstrap. Return true if handled.
        // TODO: This may change
        //
        virtual bool onBootstrap(Struct data) {
            return false;
        }

        //
        // For plugins discovered during bootstrap, permits discovering other
        // plugins. Return true if handled.
        // TODO: This may change
        //
        virtual bool onDiscover(Struct data) {
            return false;
        }

        //
        // Plugin is about to move into an active state. Return true if handled.
        //
        virtual bool onStart(Struct data) {
            return false;
        }

        //
        // Plugin has transitioned into an active state. Return true if handled.
        //
        virtual bool onRun(Struct data) {
            return false;
        }

        //
        // Plugin is being terminated - use for cleanup. Return true if handled.
        //
        virtual bool onTerminate(Struct data) {
            return false;
        }
    };

    inline const StringOrd Plugin::BOOTSTRAP{"bootstrap"};
    inline const StringOrd Plugin::DISCOVER{"discover"};
    inline const StringOrd Plugin::START{"start"};
    inline const StringOrd Plugin::RUN{"run"};
    inline const StringOrd Plugin::TERMINATE{"terminate"};

} // namespace ggapi
