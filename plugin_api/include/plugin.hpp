#pragma once
#include "cpp_api.hpp"
#include <atomic>

namespace ggapi {

    class Plugin {
    private:
        std::atomic<Scope> _moduleScope{Scope{}};
        std::atomic<StringOrd> _phase{StringOrd{}};

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
            // true on success (no exception), false on exception
            return trapErrorReturn<bool>([this, moduleHandle, phase, data]() {
                lifecycle(Scope{moduleHandle}, StringOrd{phase}, Struct{data});
                return true;
            });
        }

        //
        // Generic lifecycle - may be overridden and extended
        //
        virtual void lifecycle(Scope moduleScope, StringOrd phase, Struct data) {
            _moduleScope = moduleScope;
            _phase = phase;
            if(phase == BOOTSTRAP) {
                onBootstrap(data);
            } else if(phase == DISCOVER) {
                onDiscover(data);
            } else if(phase == START) {
                onStart(data);
            } else if(phase == RUN) {
                onRun(data);
            } else if(phase == TERMINATE) {
                onTerminate(data);
            }
            // All unhandled phases are ignored by design
        }

        //
        // Retrieve scope of plugin
        //
        Scope getScope() {
            return _moduleScope.load();
        }

        //
        // Current phase driven by lifecycle manager
        //
        StringOrd getCurrentPhase() {
            return _phase.load();
        }

        //
        // For plugins discovered during bootstrap
        // TODO: This may change
        //
        virtual void onBootstrap(Struct data) {
        }

        //
        // For plugins discovered during bootstrap, permits discovering other
        // plugins.
        // TODO: This may change
        //
        virtual void onDiscover(Struct data) {
        }

        //
        // Plugin is about to move into an active state
        //
        virtual void onStart(Struct data) {
        }

        //
        // Plugin has transitioned into an active state
        //
        virtual void onRun(Struct data) {
        }

        //
        // Plugin is being terminated - use for cleanup
        //
        virtual void onTerminate(Struct data) {
        }
    };

    inline const StringOrd Plugin::BOOTSTRAP{"bootstrap"};
    inline const StringOrd Plugin::DISCOVER{"discover"};
    inline const StringOrd Plugin::START{"start"};
    inline const StringOrd Plugin::RUN{"run"};
    inline const StringOrd Plugin::TERMINATE{"terminate"};

} // namespace ggapi
