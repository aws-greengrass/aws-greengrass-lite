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

    public:
        // Mapping of symbols to enums
        inline static const util::LookupTable EVENT_MAP{
            INITIALIZE_SYM, Events::INITIALIZE, START_SYM, Events::START, STOP_SYM, Events::STOP};

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
                *pHandled = lifecycle(Symbol{event}, ObjHandle::of<Struct>(data));
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
         * @brief Event Handlers for lifecycle events
         * Lifecycle event handlers return true if the lifecycle step is complete and false if there
         * is an error. Nucleus will do an orderly shutdown of affected plugins if a false response
         * is sent. The plugin must apply any retries or other recovery techniques before returning
         * TRUE/FALSE.  The response from the plugin is considered the authoritative response and
         * nucleus will react accordingly.
         *
         * NOTE: If an error occured, AND YOU RETURN FALSE, you will recieve a STOP event.  If
         * special cleanup is required due to the error condition, you must retain any state for the
         * STOP cleanup.  Any retries or other steps to recover from an error must happen INSIDE
         * this function.
         */

        /**
         * Initialize any state and prepare to run.
         * Return TRUE, if no error.
         * Return FALSE if an error occured.
         *
         * Any declared dependencies are running so LPC interactions are permitted.
         *
         * Default implementation returns TRUE.
         */
        // NOLINTNEXTLINE(performance-unnecessary-value-param) Override may modify data
        virtual bool onInitialize(Struct data) {
            std::cout << "Default onInitialize\n";
            return true;
        }

        /**
         * Begin operations.
         * start any threads, respond to LPC messages, post LPC messages.
         * Return TRUE, if no error.
         * Return FALSE, if an error occured.
         *
         * default implementation returns TRUE;
         */
        // NOLINTNEXTLINE(performance-unnecessary-value-param) Override may modify data
        virtual bool onStart(Struct data) {
            std::cout << "Default onStart\n";
            return false;
        }

        /**
         * Stop operations and cleanup
         * This event happens when a normal shutdown is happening (no errors) and also when an error
         * has been reported.  The plugin must take appropriate action for either condition.
         * Return TRUE if no error.
         * Return FALSE if an error.
         *
         * If this is an orderly shutdown and you have an error in the STOP (return FALSE) you will
         * get a STOP event a second time.
         *
         */
        // NOLINTNEXTLINE(performance-unnecessary-value-param) Override may modify data
        virtual bool onStop(Struct data) {
            std::cout << "Default onStop\n";
            return false;
        }
    };
} // namespace ggapi
