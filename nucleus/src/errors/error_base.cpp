#include "errors/error_base.hpp"
#include "scope/context_full.hpp"
#include <api_errors.hpp>

namespace errors {
    ggapiErrorKind ThreadErrorContainer::fetchKindAsInt() {
        return scope::thread()->getThreadErrorDetail().kind().asInt();
    }

    const char *ThreadErrorContainer::getCachedWhat() const {
        if(hasError()) {
            return scope::thread()->getThreadErrorDetail().what();
        } else {
            return nullptr;
        }
    }
    std::optional<Error> ThreadErrorContainer::getError() const {
        if(hasError()) {
            return scope::thread()->getThreadErrorDetail();
        } else {
            return {};
        }
    }
    ggapiErrorKind ThreadErrorContainer::setError(const Error &error) {
        scope::thread()->setThreadErrorDetail(error);
        ggapiErrorKind id = error.kind().asInt();
        _kindSymbolId = id;
        return id;
    }

    void ThreadErrorContainer::reset() noexcept {
        // Defer caching until first access - most of the time
        // calls will alternate between UNKNOWN and NO_ERROR
        _kindSymbolId = KIND_UNKNOWN;
    }
    void ThreadErrorContainer::clear() {
        if(_kindSymbolId == KIND_NO_ERROR) {
            return;
        }
        scope::thread()->setThreadErrorDetail(Error(data::Symbol{}, ""));
        _kindSymbolId = KIND_NO_ERROR;
    }

    traits::ErrorTraits::SymbolType traits::ErrorTraits::translateKind(
        std::string_view strKind) noexcept {
        return scope::context()->symbols().intern(strKind);
    }
    traits::ErrorTraits::SymbolType traits::ErrorTraits::translateKind(
        traits::ErrorTraits::SymbolType symKind) noexcept {
        return symKind;
    }
    traits::ErrorTraits::SymbolType traits::ErrorTraits::translateKind(
        ggapiErrorKind intKind) noexcept {
        return scope::context()->symbolFromInt(intKind);
    }
    traits::ErrorTraits::SymbolType traits::ErrorTraits::translateKind(
        ggapi::Symbol symKind) noexcept {
        return translateKind(symKind.asInt());
    }
} // namespace errors
