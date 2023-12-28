#pragma once
#include "api_errors.hpp"
#include "containers.hpp"
#include "handles.hpp"
#include "logging.hpp"

namespace ggapi {

    //
    // Specialization of logging used by plugins
    //

    struct LoggingTraits {
        using SymbolType = ggapi::Symbol;
        using SymbolArgType = SymbolType;
        using ArgType = ggapi::Container::ArgValue;
        using StructType = ggapi::Struct;
        using StructArgType = StructType;
        using ErrorType = ggapi::GgApiError;

        static ggapi::Symbol intern(std::string_view sv) {
            return {sv};
        }
        static void setLevel(SymbolArgType level) {
            ::ggapi::callApi([level]() { ::ggapiSetLogLevel(level.asInt()); });
        }
        static ggapi::Symbol getLevel(uint64_t &counter, SymbolArgType cachedLevel) {

            auto newLevel = ggapi::callApiReturn<uint32_t>([&counter, cachedLevel]() {
                return ::ggapiGetLogLevel(&counter, cachedLevel.asInt());
            });
            return ggapi::Symbol(newLevel);
        }
        static void logEvent(StructArgType entry) {
            ggapi::callApi([entry]() { return ::ggapiLogEvent(entry.getHandleId()); });
        }
        static StructType newStruct() {
            return ggapi::Struct::create();
        }
        static StructType cloneStruct(StructArgType s) {
            return s.clone();
        }
        static void putStruct(StructArgType s, SymbolArgType key, const ArgType &value) {
            s.put(key, value);
        }
        static std::shared_ptr<logging::LogManagerBase<LoggingTraits>> getManager() {
            static auto singleton{std::make_shared<logging::LogManagerBase<LoggingTraits>>()};
            return singleton;
        }
    };

    using LogManager = logging::LogManagerBase<LoggingTraits>;
    using Logger = logging::LoggerBase<LoggingTraits>;

} // namespace ggapi
