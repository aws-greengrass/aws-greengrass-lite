#include "errors/error_base.hpp"
#include "scope/context_full.hpp"

namespace errors {
    //
    data::Symbol::Partial ThreadErrorContainer::getKindPartial() const noexcept {
        return data::Symbol::Partial(_kindSymbolId);
    }
    data::Symbol ThreadErrorContainer::getCachedKind() const {
        if(hasError()) {
            return scope::context().symbolFromInt(_kindSymbolId);
        } else {
            return {};
        }
    }
    const char *ThreadErrorContainer::getCachedWhat() const {
        if(hasError()) {
            return scope::thread().getThreadErrorDetail().what();
        } else {
            return nullptr;
        }
    }
    std::optional<Error> ThreadErrorContainer::getError() const {
        if(hasError()) {
            return scope::thread().getThreadErrorDetail();
        } else {
            return {};
        }
    }
    void ThreadErrorContainer::setError(const Error &error) {
        scope::thread().setThreadErrorDetail(error);
        _kindSymbolId = error.kind().asInt();
    }
    void ThreadErrorContainer::clear() {
        _kindSymbolId = 0;
        scope::thread().setThreadErrorDetail(Error({}, ""));
    }
    void ThreadErrorContainer::throwIfError() {
        // Wrap in fast check
        if(hasError()) {
            // Slower path
            auto lastError = getError();
            clear();
            if(lastError.has_value()) {
                throw Error(lastError.value());
            }
        }
    }

    data::Symbol Error::kind(std::string_view kind) {
        return scope::context().symbols().intern(kind);
    }

    template<>
    Error Error::of<ggapi::GgApiError>(const ggapi::GgApiError &error) {
        data::Symbol s = scope::context().symbolFromInt(error.kind().asInt());
        return Error(s, std::string(error.what()));
    }

} // namespace errors
