#pragma once
#include "cpp_api.hpp"
#include "util.hpp"
#include <atomic>

namespace ggapi {

    /**
     * Base class for all plugins
     */
    class Plugin {
    public:
        enum class Phase { UNKNOWN, BOOTSTRAP, BIND, DISCOVER, START, RUN, TERMINATE };
        using PhaseEnum = util::Enum<
            Phase,
            Phase::UNKNOWN,
            Phase::BOOTSTRAP,
            Phase::BIND,
            Phase::DISCOVER,
            Phase::START,
            Phase::RUN,
            Phase::TERMINATE>;

    private:
        std::atomic<ModuleScope> _moduleScope{ModuleScope{}};
        std::atomic<Phase> _phase{Phase::UNKNOWN};
        std::atomic<Struct> _config{};

        bool lifecycleDispatch(const PhaseEnum::ConstType<Phase::BOOTSTRAP> &, Struct data) {
            return onBootstrap(data);
        }

        bool lifecycleDispatch(const PhaseEnum::ConstType<Phase::BIND> &, Struct data) {
            internalBind(data);
            return onBind(data);
        }

        bool lifecycleDispatch(const PhaseEnum::ConstType<Phase::DISCOVER> &, Struct data) {
            return onDiscover(data);
        }

        bool lifecycleDispatch(const PhaseEnum::ConstType<Phase::START> &, Struct data) {
            return onStart(data);
        }

        bool lifecycleDispatch(const PhaseEnum::ConstType<Phase::RUN> &, Struct data) {
            return onRun(data);
        }

        bool lifecycleDispatch(const PhaseEnum::ConstType<Phase::TERMINATE> &, Struct data) {
            return onTerminate(data);
        }

        bool lifecycleDispatch(const PhaseEnum::ConstType<Phase::UNKNOWN> &, Struct data) {
            return false;
        }

        void internalBind(Struct data) {
            _config = getScope().anchor(data.get<ggapi::Struct>(CONFIG));
        }
        // Lifecycle constants
        inline static const Symbol BOOTSTRAP_SYM{"bootstrap"};
        inline static const Symbol BIND_SYM{"bind"};
        inline static const Symbol DISCOVER_SYM{"discover"};
        inline static const Symbol START_SYM{"start"};
        inline static const Symbol RUN_SYM{"run"};
        inline static const Symbol TERMINATE_SYM{"terminate"};

    public:
        // Mapping of symbols to enums
        inline static const util::LookupTable PHASE_MAP{
            BOOTSTRAP_SYM,
            Phase::BOOTSTRAP,
            BIND_SYM,
            Phase::BIND,
            DISCOVER_SYM,
            Phase::DISCOVER,
            START_SYM,
            Phase::START,
            RUN_SYM,
            Phase::RUN,
            TERMINATE_SYM,
            Phase::TERMINATE};

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
            auto mappedPhase = PHASE_MAP.lookup(phase).value_or(Phase::UNKNOWN);
            _phase = mappedPhase;
            beforeLifecycle(phase, data); // TODO: Deprecate
            beforeLifecycle(mappedPhase, data);
            bool handled = PhaseEnum::visit<bool>(mappedPhase, [this, data](auto p) {
                               return this->lifecycleDispatch(p, data);
                           }).value_or(false);
            afterLifecycle(mappedPhase, data);
            afterLifecycle(phase, data); // TODO: Deprecate
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
        [[nodiscard]] Phase getCurrentPhase() const {
            return _phase.load();
        }

        /**
         * Retrieve config space unique to the given plugin
         */
        [[nodiscard]] Struct getConfig() const {
            return _config;
        }

        /**
         * (Deprecated) Hook to allow any pre-processing before lifecycle step
         */
        virtual void beforeLifecycle(Symbol phase, Struct data) {
        }

        /**
         * Hook to allow any pre-processing before lifecycle step
         */
        virtual void beforeLifecycle(Phase phase, Struct data) {
        }

        /**
         * (Deprecated) Hook to allow any post-processing after lifecycle step
         */
        virtual void afterLifecycle(Symbol phase, Struct data) {
        }

        /**
         * Hook to allow any post-processing after lifecycle step
         */
        virtual void afterLifecycle(Phase phase, Struct data) {
        }

        /**
         * For plugins discovered during bootstrap. Return true if handled. Typically a plugin
         * will set the component name during this cycle.
         * TODO: This may change
         */
        virtual bool onBootstrap(Struct data) {
            std::cout << "Default onBootstrap\n";
            return false;
        }

        /**
         * For plugins, after recipe has been read, but before any other
         * lifecycle stages. Use this cycle for any data binding.
         */
        virtual bool onBind(Struct data) {
            std::cout << "Default onBind\n";
            return false;
        }

        /**
         * For plugins discovered during bootstrap, permits discovering other
         * plugins. Return true if handled.
         * TODO: This may change
         */
        virtual bool onDiscover(Struct data) {
            std::cout << "Default onDiscover\n";
            return false;
        }

        /**
         * Plugin is about to move into an active state. Return true if handled.
         */
        virtual bool onStart(Struct data) {
            std::cout << "Default onStart\n";
            return false;
        }

        /**
         * Plugin has transitioned into an active state. Return true if handled.
         */
        virtual bool onRun(Struct data) {
            std::cout << "Default onRun\n";
            return false;
        }

        /**
         * Plugin is being terminated - use for cleanup. Return true if handled.
         */
        virtual bool onTerminate(Struct data) {
            std::cout << "Default onTerminate\n";
            return false;
        }
    };

} // namespace ggapi
