#pragma once
#include "cpp_api.hpp"
#include <atomic>
#include <map>

namespace ggapi {
    /**
     * Base class for all plugins
     */
    class Plugin {
    public:
        enum class Events { INITIALIZE, START, STOP, UNKNOWN };
        using EventEnum =
            util::Enum<Events, Events::INITIALIZE, Events::START, Events::STOP, Events::UNKNOWN>;

    private:
        mutable std::shared_mutex _baseMutex; // Unique name to simplify debugging
        ModuleScope _moduleScope{ModuleScope{}};
        Struct _config{};

        void lifecycleDispatch(const EventEnum::ConstType<Events::INITIALIZE> &, Struct data) {
            internalBind(data);
            onInitialize(std::move(data));
        }

        void lifecycleDispatch(const EventEnum::ConstType<Events::START> &, Struct data) {
            onStart(std::move(data));
        }

        void lifecycleDispatch(const EventEnum::ConstType<Events::STOP> &, Struct data) {
            onStop(std::move(data));
        }

        static void lifecycleDispatch(
            const EventEnum::ConstType<Events::UNKNOWN> &, const Struct &) {
            // add a log message here for the unknown event
        }

    protected:
        // Exposed for testing by inheritance

        void internalBind(const Struct &data) {
            auto moduleScope = data.get<ggapi::ModuleScope>(MODULE);
            auto config = data.get<ggapi::Struct>(CONFIG);
            std::unique_lock guard{_baseMutex};
            if(moduleScope) {
                _moduleScope = moduleScope;
            }
            if(config) {
                _config = config;
            }
        }
        // Lifecycle constants
        inline static const Symbol INITIALIZE_SYM{"initialize"};
        inline static const Symbol START_SYM{"start"};
        inline static const Symbol STOP_SYM{"stop"};

    public:
        // Mapping of symbols to enums
        inline static const util::LookupTable EVENT_MAP{
            INITIALIZE_SYM, Events::INITIALIZE, START_SYM, Events::START, STOP_SYM, Events::STOP};

        // Lifecycle parameter constants
        inline static const Symbol CONFIG_ROOT{"configRoot"};
        inline static const Symbol CONFIG{"config"};
        inline static const Symbol SYSTEM{"system"};
        inline static const Symbol NUCLEUS_CONFIG{"nucleus"};
        inline static const Symbol NAME{"name"};
        inline static const Symbol MODULE{"module"};

        Plugin() noexcept = default;
        Plugin(const Plugin &) = delete;
        Plugin(Plugin &&) noexcept = delete;
        Plugin &operator=(const Plugin &) = delete;
        Plugin &operator=(Plugin &&) noexcept = delete;
        // TODO: make this noexcept
        virtual ~Plugin() = default;

        ggapiErrorKind lifecycle(
            ggapiObjHandle, // TODO: Remove
            ggapiSymbol event,
            ggapiObjHandle data) noexcept {
            // No exceptions may cross API boundary
            // Return true if handled.
            return ggapi::catchErrorToKind(
                [this, event, data]() { lifecycle(Symbol{event}, ObjHandle::of<Struct>(data)); });
        }

        /**
         * Retrieve the active module scope associated with plugin
         */
        [[nodiscard]] ModuleScope getModule() const {
            std::shared_lock guard{_baseMutex};
            return _moduleScope;
        }


        /**
         * Exposed for testing
         */
        bool lifecycle(Symbol event, Struct data) {
            auto mappedEvent = EVENT_MAP.lookup(event).value_or(Events::UNKNOWN);
            EventEnum::visitNoRet(
                mappedEvent, [this, data](auto p) { this->lifecycleDispatch(p, data); });
        }

    protected:
        /**
         * Retrieve config space unique to the given plugin
         */
        [[nodiscard]] Struct getConfig() const {
            std::shared_lock guard{_baseMutex};
            return _config;
        }

        /**
         * For plugins discovered during bootstrap. Return true if handled. Typically a plugin
         * will set the component name during this cycle.
         * TODO: This may change
         */
        // NOLINTNEXTLINE(performance-unnecessary-value-param) Override may modify data
        virtual void onInitialize(Struct data) {
            /* TODO: remove the std::cout in favor of logging */
            std::cout << "Default onInitialize\n";
        }

        /**
         * For plugins, after recipe has been read, but before any other
         * lifecycle stages. Use this cycle for any data binding.
         */
        // NOLINTNEXTLINE(performance-unnecessary-value-param) Override may modify data
        virtual void onStart(Struct data) {
            std::cout << "Default onStart\n";
        }

        /**
         * Plugin has transitioned into an active state. Return true if handled.
         */
        // NOLINTNEXTLINE(performance-unnecessary-value-param) Override may modify data
        virtual void onStop(Struct data) {
            std::cout << "Default onStop\n";
        }
    };
}; // namespace ggapi
