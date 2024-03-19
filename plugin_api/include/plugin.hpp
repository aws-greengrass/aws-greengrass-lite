#pragma once
#include "cpp_api.hpp"
#include "util.hpp"
#include <atomic>
#include <map>

namespace ggapi {

    /**
     * Base class for all plugins
     */
    class Plugin {
    public:
        enum class Events { INITIALIZE, START, STOP, ERROR_STOP, UNKNOWN };
        using EventEnum = util::Enum<
            Events,
            Events::INITIALIZE,
            Events::START,
            Events::STOP,
            Events::ERROR_STOP,
            Events::UNKNOWN>;

    private:
        mutable std::shared_mutex _baseMutex; // Unique name to simplify debugging
        ModuleScope _moduleScope{ModuleScope{}};
        Struct _config{};

        bool lifecycleDispatch(const EventEnum::ConstType<Events::INITIALIZE> &, Struct data) {
            internalBind(data);
            return onInitialize(std::move(data));
        }

        bool lifecycleDispatch(const EventEnum::ConstType<Events::START> &, Struct data) {
            return onStart(std::move(data));
        }

        bool lifecycleDispatch(const EventEnum::ConstType<Events::STOP> &, Struct data) {
            return onStop(std::move(data));
        }

        bool lifecycleDispatch(const EventEnum::ConstType<Events::ERROR_STOP> &, Struct data) {
            return onError_stop(std::move(data));
        }

        static bool lifecycleDispatch(
            const EventEnum::ConstType<Events::UNKNOWN> &, const Struct &) {
            return false;
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
        inline static const Symbol ERROR_STOP_SYM{"error_stop"};

    public:
        // Mapping of symbols to enums
        inline static const util::LookupTable EVENT_MAP{
            INITIALIZE_SYM,
            Events::INITIALIZE,
            START_SYM,
            Events::START,
            STOP_SYM,
            Events::STOP,
            ERROR_STOP_SYM,
            Events::ERROR_STOP};

        // Lifecycle parameter constants
        inline static const Symbol CONFIG_ROOT{"configRoot"};
        inline static const Symbol CONFIG{"config"};
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
            ggapiObjHandle data,
            bool *pHandled) noexcept {
            // No exceptions may cross API boundary
            // Return true if handled.
            return ggapi::catchErrorToKind([this, event, data, pHandled]() {
                *pHandled = lifecycle(
                    Symbol{event},
                    ObjHandle::of<Struct>(data));
            });
        }

        /**
         * Retrieve the active module scope associated with plugin
         */
        [[nodiscard]] ModuleScope getModule() const {
            std::shared_lock guard{_baseMutex};
            return _moduleScope;
        }

    protected:
        bool lifecycle(Symbol event, Struct data) {
            auto mappedEvent = EVENT_MAP.lookup(event).value_or(Events::UNKNOWN);
            bool handled = EventEnum::visit<bool>(mappedEvent, [this, data](auto p) {
                               return this->lifecycleDispatch(p, data);
                           }).value_or(false);
            return handled;
        }

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
        virtual bool onInitialize(Struct data) {
            std::cout << "Default onInitialize\n";
            return false;
        }

        /**
         * For plugins, after recipe has been read, but before any other
         * lifecycle stages. Use this cycle for any data binding.
         */
        // NOLINTNEXTLINE(performance-unnecessary-value-param) Override may modify data
        virtual bool onStart(Struct data) {
            std::cout << "Default onStart\n";
            return false;
        }

        /**
         * Plugin has transitioned into an active state. Return true if handled.
         */
        // NOLINTNEXTLINE(performance-unnecessary-value-param) Override may modify data
        virtual bool onStop(Struct data) {
            std::cout << "Default onStop\n";
            return false;
        }

        /**
         * Plugin is being terminated - use for cleanup. Return true if handled.
         */
        // NOLINTNEXTLINE(performance-unnecessary-value-param) Override may modify data
        virtual bool onError_stop(Struct data) {
            std::cout << "Default onError_stop\n";
            return false;
        }
    };
} // namespace ggapi
