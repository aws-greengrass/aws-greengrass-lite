#pragma once
#include "cpp_api.hpp"
#include <atomic>

namespace ggapi {

    /**
     * Base class for all plugins
     */
    class Plugin {
    private:
        std::atomic<ModuleScope> _moduleScope{ModuleScope{}};
        std::atomic<Symbol> _phase{Symbol{}};
        std::atomic<Struct> _config{};

        /**
         * Generic lifecycle dispatch
         */
        bool lifecycleDispatch(Symbol phase, Struct data) {
            if(phase == BOOTSTRAP) {
                return onBootstrap(data);
            } else if(phase == BIND) {
                internalBind(data);
                return onBind(data);
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

        void internalBind(Struct data) {
            _config = getScope().anchor(data.get<ggapi::Struct>(CONFIG));
        }

    public:
        // Lifecycle constants
        inline static const Symbol BOOTSTRAP{"bootstrap"};
        inline static const Symbol BIND{"bind"};
        inline static const Symbol DISCOVER{"discover"};
        inline static const Symbol START{"start"};
        inline static const Symbol RUN{"run"};
        inline static const Symbol TERMINATE{"terminate"};

        // Lifecycle parameter constants
        inline static const Symbol CONFIG_ROOT{"configRoot"};
        inline static const Symbol CONFIG{"config"};
        inline static const Symbol NUCLEUS_CONFIG{"nucleus"};
        inline static const Symbol NAME{"name"};

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
                return lifecycle(ModuleScope{moduleHandle}, Symbol{phase}, Struct{data});
            });
        }

        bool lifecycle(ModuleScope moduleScope, Symbol phase, Struct data) {
            _moduleScope = moduleScope;
            _phase = phase;
            beforeLifecycle(phase, data);
            bool handled = lifecycleDispatch(phase, data);
            afterLifecycle(phase, data);
            return handled;
        }

        /**
         * Retrieve scope of plugin. Using getScope().anchor() will attach data to the module
         * scope.
         */
        [[nodiscard]] ModuleScope getScope() const {
            return _moduleScope.load();
        }

        /**
         * Current phase driven by lifecycle manager
         */
        [[nodiscard]] Symbol getCurrentPhase() const {
            return _phase.load();
        }

        /**
         * Retrieve config space unique to the given plugin
         */
        [[nodiscard]] Struct getConfig() const {
            return _config;
        }

        /**
         * Hook to allow any pre-processing before lifecycle step
         */
        virtual void beforeLifecycle(Symbol phase, Struct data) {
        }

        /**
         * Hook to allow any post-processing after lifecycle step
         */
        virtual void afterLifecycle(Symbol phase, Struct data) {
        }

        /**
         * For plugins discovered during bootstrap. Return true if handled. Typically a plugin
         * will set the component name during this cycle.
         * TODO: This may change
         */
        virtual bool onBootstrap(Struct data) {
            return false;
        }

        /**
         * For plugins, after recipe has been read, but before any other
         * lifecycle stages. Use this cycle for any data binding.
         */
        virtual bool onBind(Struct data) {
            return false;
        }

        /**
         * For plugins discovered during bootstrap, permits discovering other
         * plugins. Return true if handled.
         * TODO: This may change
         */
        virtual bool onDiscover(Struct data) {
            return false;
        }

        /**
         * Plugin is about to move into an active state. Return true if handled.
         */
        virtual bool onStart(Struct data) {
            return false;
        }

        /**
         * Plugin has transitioned into an active state. Return true if handled.
         */
        virtual bool onRun(Struct data) {
            return false;
        }

        /**
         * Plugin is being terminated - use for cleanup. Return true if handled.
         */
        virtual bool onTerminate(Struct data) {
            return false;
        }
    };

} // namespace ggapi
