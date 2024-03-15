#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <util.hpp>

/**
 * GG-Interop: Logging API is intentionally replicating the facility built into Greengrass-Java
 * Nucleus. When logging an item, it isn't simply logging a string, but logging structured data.
 * A characteristic of logging is to do minimal work when a log-level is disabled, and to support
 * different log levels based on the logging tag. Most of the logging implementation is in
 * the Nucleus. The tags used should ideally match the tags used in Greengrass-Java where possible.
 *
 * See also https://github.com/aws-greengrass/aws-greengrass-logging-java
 */
namespace logging {

    enum class Level { None, Trace, Debug, Info, Warn, Error };
    enum class Format { Text, Json };
    enum class OutputType { File, Console };

    template<typename Traits>
    class LogManagerBase;

    template<typename Traits>
    class LoggerBase;

    template<typename Traits>
    class Event;

    /**
     * Interface for the Logger implementation
     */
    template<typename Traits>
    class LogManagerBase : public util::RefObject<LogManagerBase<Traits>> {

    public:
        using SymbolType = typename Traits::SymbolType;
        using SymbolArgType = typename Traits::SymbolArgType;
        using StructArgType = typename Traits::StructArgType;

        const SymbolType NONE_LEVEL{Traits::intern("NONE")};
        const SymbolType TRACE_LEVEL{Traits::intern("TRACE")};
        const SymbolType DEBUG_LEVEL{Traits::intern("DEBUG")};
        const SymbolType INFO_LEVEL{Traits::intern("INFO")};
        const SymbolType WARN_LEVEL{Traits::intern("WARN")};
        const SymbolType ERROR_LEVEL{Traits::intern("ERROR")};

        // GG-Interop: These are already defined for Greengrass Logging
        const SymbolType CAUSE_KEY{Traits::intern("cause")};
        const SymbolType CONTEXTS_KEY{Traits::intern("contexts")};
        const SymbolType EVENT_KEY{Traits::intern("event")};
        const SymbolType LEVEL_KEY{Traits::intern("level")};
        const SymbolType LOGGER_NAME_KEY{Traits::intern("loggerName")};
        const SymbolType MESSAGE_KEY{Traits::intern("message")};
        const SymbolType TIMESTAMP_KEY{Traits::intern("timestamp")};
        const SymbolType CAUSE_MESSAGE_KEY{Traits::intern("message")};
        // Keys unique to GG-Lite
        const SymbolType CAUSE_KIND_KEY{Traits::intern("kind")};
        const SymbolType MODULE_KEY{Traits::intern("component")};

        const util::LookupTable<SymbolType, Level, 5> LEVEL_MAP{
            TRACE_LEVEL,
            Level::Trace,
            DEBUG_LEVEL,
            Level::Debug,
            INFO_LEVEL,
            Level::Info,
            WARN_LEVEL,
            Level::Warn,
            ERROR_LEVEL,
            Level::Error};

    public:
        LogManagerBase() = default;

        LogManagerBase(const LogManagerBase &) noexcept = delete;

        LogManagerBase(LogManagerBase &&) noexcept = delete;

        LogManagerBase &operator=(const LogManagerBase &) noexcept = delete;

        LogManagerBase &operator=(LogManagerBase &&) noexcept = delete;

        virtual ~LogManagerBase() noexcept = default;

        SymbolType toSymbol(Level level) const {
            return LEVEL_MAP.rlookup(level).value_or(NONE_LEVEL);
        }

        Level toLevel(SymbolArgType level) const {
            return LEVEL_MAP.lookup(level).value_or(Level::None);
        }

        virtual void setLevel(Level level) {
            auto levelSymbol = toSymbol(level);
            Traits::setLevel(levelSymbol);
        }

        virtual Level getLevel(uint64_t &counter, Level priorLevel) const {
            auto levelSymbol = toSymbol(priorLevel);
            auto newSymbol = Traits::getLevel(counter, levelSymbol);
            if(newSymbol == levelSymbol) {
                return priorLevel;
            } else {
                return toLevel(newSymbol);
            }
        }

        virtual void logEvent(StructArgType entry) {
            Traits::logEvent(entry);
        }

        LoggerBase<Traits> getLogger(SymbolArgType loggerName) noexcept;
    };

    namespace detail {
        template<typename Traits>
        class LoggerImpl;

        template<typename Traits>
        struct EventImplBase;

        /**
         * Interface for the Logger implementation
         */
        template<typename Traits>
        class LoggerImpl : public util::RefObject<LoggerImpl<Traits>> {
        public:
            using SymbolType = typename Traits::SymbolType;
            using SymbolArgType = typename Traits::SymbolArgType;
            using ArgValue = typename Traits::ArgType;
            using StructType = typename Traits::StructType;
            using StructArgType = typename Traits::StructArgType;

        private:
            const std::shared_ptr<LogManagerBase<Traits>> _manager;
            const SymbolType _loggerName;
            mutable std::atomic_uint64_t _counter{0};
            mutable std::atomic<Level> _cachedLevel{Level::None};
            // Note, context cannot be allocated in static section. Logging is usually declared in
            // static section. Solution is to create context only when needed.
            mutable std::shared_mutex _mutex;
            mutable StructType _context;

            [[nodiscard]] StructType maybeCloneContext() const {
                std::shared_lock guard{_mutex};
                if(_context) {
                    return Traits::cloneStruct(_context);
                } else {
                    return {};
                }
            }

        public:
            LoggerImpl(const LoggerImpl &other)
                : _manager(other._manager), _loggerName(other._loggerName),
                  _context(maybeCloneContext()) {
            }

            LoggerImpl(LoggerImpl &&) = delete;

            LoggerImpl &operator=(const LoggerImpl &) = delete;

            LoggerImpl &operator=(LoggerImpl &&) = delete;

            ~LoggerImpl() noexcept = default;

            LoggerImpl(
                const std::shared_ptr<LogManagerBase<Traits>> &manager, SymbolArgType loggerName)
                : _manager(manager), _loggerName(loggerName) {
            }

            void addKV(SymbolArgType key, const ArgValue &val) {
                std::shared_lock useGuard{_mutex};
                if(!_context) {
                    // Create shared context on demand
                    useGuard.unlock();
                    std::unique_lock createGuard{_mutex};
                    if(!_context) {
                        _context = Traits::newStruct();
                    }
                    createGuard.unlock();
                    useGuard.lock();
                }
                Traits::putStruct(_context, key, val);
            }

            [[nodiscard]] StructType cloneContext() const {
                auto cloned = maybeCloneContext();
                if(cloned) {
                    return cloned;
                } else {
                    return Traits::newStruct();
                }
            }

            void commit(StructArgType entry) {
                Traits::putStruct(entry, _manager->LOGGER_NAME_KEY, _loggerName);
                _manager->logEvent(entry);
            }

            [[nodiscard]] SymbolType getLoggerName() const {
                return _loggerName;
            }

            [[nodiscard]] Level getLevel() const {
                // Race condition - cachedLevel may be newer than counter - that's ok,
                // the older counter will cause us to synchronize
                // We're risking/handling this race for sake of saving a mutex
                // the purpose of counter is to minimize expensive code path in lookup
                uint64_t counter = _counter;
                Level cachedLevel = _cachedLevel;
                Level newLevel = _manager->getLevel(counter, cachedLevel);
                if(counter != _counter) {
                    // Race condition - _counter could increment before/after setting level
                    // If that happens, we'll essentially set counter to older value, which
                    // causes us to discard _cachedLevel - so ok
                    _cachedLevel = newLevel;
                    _counter = counter;
                }
                return newLevel;
            }

            [[nodiscard]] bool isEnabled(Level level) const {
                if(level == Level::None) {
                    return false;
                }
                Level current = getLevel();
                if(current == Level::None) {
                    return false;
                } else {
                    return current <= level;
                }
            }

            [[nodiscard]] std::shared_ptr<EventImplBase<Traits>> atLevel(Level level);

            void setLevel(Level level) const {
                _manager->setLevel(_loggerName, level);
            }

            [[nodiscard]] std::shared_ptr<LoggerImpl> clone() const {
            }
        };

        /**
         * Interface for event implementation
         */
        template<typename Traits>
        struct EventImplBase {
            using SymbolArgType = typename Traits::SymbolArgType;
            using ArgValue = typename Traits::ArgType;
            using ErrorType = typename Traits::ErrorType;

            EventImplBase() noexcept = default;

            EventImplBase(const EventImplBase &) noexcept = default;

            EventImplBase(EventImplBase &&) noexcept = default;

            EventImplBase &operator=(const EventImplBase &) noexcept = default;

            EventImplBase &operator=(EventImplBase &&) noexcept = default;

            virtual ~EventImplBase() noexcept = default;

            virtual void setCause(const ErrorType &) = 0;

            virtual void setEvent(SymbolArgType) = 0;

            virtual void setMessage(const ArgValue &) = 0;

            virtual void setLazyMessage(const std::function<const ArgValue &()> &) = 0;

            virtual void addKV(SymbolArgType, const ArgValue &) = 0;

            virtual void addLazyKV(SymbolArgType, const std::function<const ArgValue &()> &) = 0;

            virtual void commit() = 0;
        };

        /**
         * Event when not logging - optimize for this use-case to do nothing
         */
        template<typename Traits>
        struct EventNoopImpl : public EventImplBase<Traits> {
            using SymbolArgType = typename Traits::SymbolArgType;
            using ArgValue = typename Traits::ArgType;
            using ErrorType = typename Traits::ErrorType;

            void setCause(const ErrorType &) override {
            }

            void setEvent(SymbolArgType) override {
            }

            void setMessage(const ArgValue &) override {
            }

            void setLazyMessage(const std::function<const ArgValue &()> &) override {
            }

            void addKV(SymbolArgType, const ArgValue &) override {
            }

            void addLazyKV(SymbolArgType, const std::function<const ArgValue &()> &) override {
            }

            void commit() override {
            }

            inline static std::shared_ptr<EventImplBase<Traits>> self() {
                const static std::shared_ptr<EventImplBase<Traits>> singleton{
                    std::make_shared<EventNoopImpl>()};
                return singleton;
            }
        };

        /**
         * Event when logging. Note that this is intended to be used by only one (current)
         * thread.
         */
        template<typename Traits>
        class EventActiveImpl : public EventImplBase<Traits> {
        public:
            using SymbolType = typename Traits::SymbolType;
            using SymbolArgType = typename Traits::SymbolArgType;
            using ArgValue = typename Traits::ArgType;
            using StructType = typename Traits::StructType;
            using ErrorType = typename Traits::ErrorType;

        private:
            std::shared_ptr<LogManagerBase<Traits>> _manager;
            std::shared_ptr<LoggerImpl<Traits>> _logger;
            StructType _context;
            StructType _data{Traits::newStruct()};
            Level _level;
            const std::chrono::system_clock::time_point _timestamp =
                std::chrono::system_clock::now();

        public:
            EventActiveImpl(
                const std::shared_ptr<LogManagerBase<Traits>> &manager,
                const std::shared_ptr<LoggerImpl<Traits>> &logger,
                Level level)
                : _manager(manager), _logger(logger), _context(logger->cloneContext()),
                  _level(level) {
                Traits::putStruct(_data, _manager->CONTEXTS_KEY, _context);
            }

            void setCause(const ErrorType &error) override {
                StructType cause{Traits::newStruct()};
                SymbolType kind = error.kind();
                std::string what;
                if(error.what() != nullptr) {
                    what = error.what();
                }
                Traits::putStruct(cause, _manager->CAUSE_KIND_KEY, kind);
                Traits::putStruct(cause, _manager->CAUSE_MESSAGE_KEY, what);
                Traits::putStruct(_data, _manager->CAUSE_KEY, cause);
                setMessage(what);
            }

            void setEvent(SymbolArgType eventType) override {
                Traits::putStruct(_data, _manager->EVENT_KEY, eventType);
            }

            void setMessage(const ArgValue &message) override {
                Traits::putStruct(_data, _manager->MESSAGE_KEY, message);
            }

            void setLazyMessage(const std::function<const ArgValue &()> &func) override {
                setMessage(func());
            }

            void addKV(SymbolArgType key, const ArgValue &value) override {
                Traits::putStruct(_context, key, value);
            }

            void addLazyKV(
                SymbolArgType key, const std::function<const ArgValue &()> &func) override {
                addKV(key, func());
            }

            void commit() override {
                Traits::putStruct(_data, _manager->LEVEL_KEY, _manager->toSymbol(_level));
                Traits::putStruct(
                    _data,
                    _manager->TIMESTAMP_KEY,
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        _timestamp.time_since_epoch())
                        .count());
                _logger->commit(_data);
            }
        };

        template<typename Traits>
        [[nodiscard]] std::shared_ptr<EventImplBase<Traits>> LoggerImpl<Traits>::atLevel(
            Level level) {
            auto self = LoggerImpl<Traits>::baseRef();
            if(isEnabled(level)) {
                return std::make_unique<EventActiveImpl<Traits>>(_manager, self, level);
            } else {
                return EventNoopImpl<Traits>::self();
            }
        }

    } // namespace detail

    /**
     * Builder to build a single event.
     */
    template<typename Traits>
    class Event {
        // Used only for active log entry
        std::shared_ptr<detail::EventImplBase<Traits>> _impl;

    public:
        using SymbolArgType = typename Traits::SymbolArgType;
        using ArgValue = typename Traits::ArgType;
        using ErrorType = typename Traits::ErrorType;

        explicit Event(const std::shared_ptr<detail::EventImplBase<Traits>> &impl) : _impl(impl) {
        }

        Event() : _impl(detail::EventNoopImpl<Traits>::self()) {
        }

        /**
         * Log a cause of error/event - this is expected to be a 'constant' symbol
         */
        Event &cause(const ErrorType &cause) {
            _impl->setCause(cause);
            return *this;
        }

        Event &cause(const std::exception &cause) {
            _impl->setCause(ErrorType::of(cause));
            return *this;
        }

        /**
         * Log an event type - this is expected to be a 'constant' symbol
         */
        Event &event(SymbolArgType eventType) {
            _impl->setEvent(eventType);
            return *this;
        }

        /**
         * Add context information to event
         */
        Event &kv(SymbolArgType key, const ArgValue &value) {
            _impl->addKV(key, value);
            return *this;
        }

        /**
         * Add context information to event with lazy evaluation as a lambda
         */
        Event &kv(SymbolArgType key, const std::function<const ArgValue &()> &fn) {
            _impl->addLazyKV(key, fn);
            return *this;
        }

        /**
         * Commit the log entry and throw exception
         */
        [[noreturn]] void logAndThrow(const ErrorType &err) {
            _impl->setCause(err);
            _impl->commit();
            throw err;
        }

        /**
         * Commit the log entry and throw exception with translation
         */
        [[noreturn]] void logAndThrow(const std::exception &err) {
            logAndThrow(ErrorType::of(err));
        }

        /**
         * Commit the log entry with no/existing message
         */
        void log() {
            _impl->commit();
        }

        /**
         * Commit the log entry with a message
         */
        void log(const ArgValue &value) {
            _impl->setMessage(value);
            _impl->commit();
        }

        /**
         * Commit the log entry with a lazy-evaluation message
         */
        void log(const std::function<const ArgValue &()> &fn) {
            _impl->setLazyMessage(fn);
            _impl->commit();
        }
    };

    /**
     * Event factory for logging to a given tag/contact
     */
    template<typename Traits>
    class LoggerBase {
        std::shared_ptr<detail::LoggerImpl<Traits>> _impl;

    public:
        using SymbolType = typename Traits::SymbolType;
        using SymbolArgType = typename Traits::SymbolArgType;
        using ArgValue = typename Traits::ArgType;

        explicit LoggerBase(const std::shared_ptr<detail::LoggerImpl<Traits>> &impl) : _impl(impl) {
        }

        /**
         * Name of logger instance
         */
        [[nodiscard]] SymbolType getLoggerName() const {
            return _impl->getLoggerName();
        }

        /**
         * Contextual information added to each event
         */
        LoggerBase &addDefaultKeyValue(SymbolArgType key, const ArgValue &value) {
            _impl->addKV(key, value);
            return *this;
        }

        /**
         * Shorter form of addDefaultKeyValue
         */
        LoggerBase &dfltKv(SymbolArgType key, const ArgValue &value) {
            _impl->addKV(key, value);
            return *this;
        }

        /**
         * Builder for an enum log level. If not logging, the returned Event is a no-op.
         */
        [[nodiscard]] Event<Traits> atLevel(Level level) const {
            return Event{_impl->atLevel(level)};
        }

        /**
         * Builder for a trace-level event. If not logging, the returned Event is a no-op.
         */
        [[nodiscard]] Event<Traits> atTrace() const {
            return atLevel(Level::Trace);
        }

        /**
         * Builder for a trace-level event. If not logging, the returned Event is a no-op.
         */
        [[nodiscard]] Event<Traits> atTrace(SymbolArgType eventType) const {
            return atTrace().event(eventType);
        }

        /**
         * Builder for a debug-level event. If not logging, the returned Event is a no-op.
         */
        [[nodiscard]] Event<Traits> atDebug() const {
            return atLevel(Level::Debug);
        }

        /**
         * Builder for a debug-level event. If not logging, the returned Event is a no-op.
         */
        [[nodiscard]] Event<Traits> atDebug(SymbolArgType eventType) const {
            return atDebug().event(eventType);
        }

        /**
         * Builder for an info-level event. If not logging, the returned Event is a no-op.
         */
        [[nodiscard]] Event<Traits> atInfo() const {
            return atLevel(Level::Info);
        }

        /**
         * Builder for an info-level event. If not logging, the returned Event is a no-op.
         */
        [[nodiscard]] Event<Traits> atInfo(SymbolArgType eventType) const {
            return atInfo().event(eventType);
        }

        /**
         * Builder for an warn-level event. If not logging, the returned Event is a no-op.
         */
        [[nodiscard]] Event<Traits> atWarn() const {
            return atLevel(Level::Warn);
        }

        /**
         * Builder for an warn-level event. If not logging, the returned Event is a no-op.
         */
        [[nodiscard]] Event<Traits> atWarn(SymbolArgType eventType) const {
            return atWarn().event(eventType);
        }

        /**
         * Builder for an error-level event. If not logging, the returned Event is a no-op.
         */
        [[nodiscard]] Event<Traits> atError() const {
            return atLevel(Level::Error);
        }

        /**
         * Builder for an error-level event. If not logging, the returned Event is a no-op.
         */
        [[nodiscard]] Event<Traits> atError(SymbolArgType eventType) const {
            return atError().event(eventType);
        }

        [[nodiscard]] bool isEnabled(Level level) const {
            return _impl->isEnabled(level);
        }

        [[nodiscard]] bool isTraceEnabled() const {
            return isEnabled(Level::Trace);
        }

        [[nodiscard]] bool isDebugEnabled() const {
            return isEnabled(Level::Debug);
        }

        [[nodiscard]] bool isInfoEnabled() const {
            return isEnabled(Level::Info);
        }

        [[nodiscard]] bool isWarnEnabled() const {
            return isEnabled(Level::Warn);
        }

        [[nodiscard]] bool isErrorEnabled() const {
            return isEnabled(Level::Error);
        }

        /**
         * Change level for this specific name
         */
        void setLevel(Level level) {
            return _impl->setLevel(level);
        }

        /**
         * Copy this instance for purpose of applying additional key/value pairs
         */
        [[nodiscard]] LoggerBase createChild() const {
            return LoggerBase(_impl->clone());
        }

        static LoggerBase of(SymbolArgType loggerName) noexcept {
            std::shared_ptr<logging::LogManagerBase<Traits>> mgr = Traits::getManager();
            return mgr->getLogger(loggerName);
        }
    };

    /**
     * Retrieve logger for given name. The returned value may be stored statically and is
     * thread safe.
     *
     * @param loggerName Name of logger
     * @return Logger facade
     */
    template<typename Traits>
    LoggerBase<Traits> LogManagerBase<Traits>::getLogger(SymbolArgType loggerName) noexcept {
        auto impl{std::make_shared<detail::LoggerImpl<Traits>>(
            LogManagerBase<Traits>::baseRef(), loggerName)};
        return LoggerBase(impl);
    }
} // namespace logging
