#include "data/shared_buffer.hpp"
#include "scope/context_full.hpp"
#include <cpp_api.hpp>

uint32_t ggapiGetLogLevel(uint64_t *counter, uint32_t level) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([counter, level]() {
        auto &context = scope::context();
        auto module = scope::thread().getEffectiveModule();
        auto levelSymbol = context.symbolFromInt(level);
        auto newSymbol = context.logManager().getLevel(module, *counter, levelSymbol);
        return newSymbol.asInt();
    });
}

bool ggapiSetLogLevel(uint32_t level) noexcept {
    return ggapi::trapErrorReturn<bool>([level]() {
        auto &context = scope::context();
        auto module = scope::thread().getEffectiveModule();
        auto levelSymbol = context.symbolFromInt(level);
        context.logManager().setLevel(module, levelSymbol);
        return true;
    });
}

bool ggapiLogEvent(uint32_t dataHandle) noexcept {
    return ggapi::trapErrorReturn<bool>([dataHandle]() {
        auto &context = scope::context();
        auto module = scope::thread().getEffectiveModule();
        auto dataStruct = context.objFromInt<data::StructModelBase>(dataHandle);
        context.logManager().logEvent(module, dataStruct);
        return true;
    });
}
