#include "errors/error_base.hpp"
#include "scope/context_full.hpp"
#include <cpp_api.hpp>

/**
 * Nucleus guarantees that the returned kind remains valid until the next
 * call to ggapiSetError() in the same thread.
 *
 * @return last error kind (a symbol)
 */
uint32_t ggapiGetErrorKind() noexcept {
    return errors::ThreadErrorContainer::get().getKindAsInt();
}

/**
 * Nucleus guarantees that the returned pointer remains valid until the next
 * call to ggapiSetError() in the same thread.
 *
 * @return last error text, or nullptr if no error
 */
const char *ggapiGetErrorWhat() noexcept {
    return errors::ThreadErrorContainer::get().getCachedWhat();
}

/**
 * Set or clear a last error state.
 *
 * @return last error text, or nullptr if no error
 */
void ggapiSetError(uint32_t kind, const char *what, size_t len) noexcept {
    static const auto DEFAULT_ERROR_WHAT = "Unspecified Error";
    try {
        if(kind == 0) {
            errors::ThreadErrorContainer::get().clear();
            return;
        }
        auto kindAsSymbol = scope::context().symbolFromInt(kind);
        std::string whatString;
        if(what == nullptr || len == 0) {
            whatString = DEFAULT_ERROR_WHAT;
        } else {
            whatString = std::string(what, len);
        }
        auto err = errors::Error(kindAsSymbol, whatString);
        errors::ThreadErrorContainer::get().setError(err);
    } catch(...) {
        // If we cannot set an error, terminate process - this is a critical condition
        std::terminate();
    }
}
