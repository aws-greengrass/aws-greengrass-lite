#include "errors/error_base.hpp"
#include "scope/context_full.hpp"
#include <cpp_api.hpp>

//
// Understanding the error passing semantics crossing C++ / C-API boundaries:
//
// TODO: Old mechanism (to remove)
// C++ code prior to calling C-API is responsible for calling ggapiSetError(0, 0, nullptr)
// this clears the error state. At end of calling C-API function, the caller can call
// ggapiGetErrorKind() to check if there was an error (like inspecting errno).
//
// New mechanism:
// With the exception of functions that are guaranteed to succeed (Symbol and error handling), each
// C-API function returns ggapiErrorKind - 0 on success, or a symbol on error. When ggapiErrorKind
// is non-zero, then a thread-safe call can be made to ggapiGetErrorWhat() to get the error text.
//
// In all cases, if there is a last error, calling ggapiGetErrorWhat will return a null-terminated
// string that is guaranteed to exist and persist until next call to ggapiSetError(). This does not
// follow the normal buffer copy semantics to reduce risk of a fatal condition when memory is low.
//
// ggapiGetErrorKind, ggapiGetErrorWhat, ggapiSetError are thread safe and guaranteed to not change
// the error state beyond their contract - in the case where the function cannot complete due to out
// of memory scenario, the process will be killed to allow a watchdog process to restart the
// process.
//

/**
 * Nucleus guarantees that the returned kind remains valid until the next
 * call to ggapiSetError() in the same thread.
 * TODO: This will be deprecated and other functions refactored once old error mechanism has been
 * fully replaced.
 *
 * @return last error kind (a symbol)
 */
extern "C" ggapiErrorKind ggapiGetErrorKind() noexcept {
    return errors::ThreadErrorContainer::get().getKindAsInt();
}

/**
 * Nucleus guarantees that the returned pointer remains valid until the next
 * call to ggapiSetError() in the same thread.
 *
 * @return last error text, or nullptr if no error
 */
extern "C" const char *ggapiGetErrorWhat() noexcept {
    return errors::ThreadErrorContainer::get().getCachedWhat();
}

/**
 * Set or clear a last error state.
 *
 * @return last error text, or nullptr if no error
 */
extern "C" ggapiErrorKind ggapiSetError(
    ggapiErrorKind kind, ggapiCountedString what, ggapiDataLen len) noexcept {
    static const auto DEFAULT_ERROR_WHAT = "Unspecified Error";
    try {
        if(kind == 0) {
            errors::ThreadErrorContainer::get().clear();
            return 0;
        }
        auto kindAsSymbol = scope::context()->symbolFromInt(kind);
        std::string whatString;
        if(what == nullptr || len == 0) {
            whatString = DEFAULT_ERROR_WHAT;
        } else {
            whatString = std::string(what, len);
        }
        auto err = errors::Error(kindAsSymbol, whatString);
        errors::ThreadErrorContainer::get().setError(err);
        return kind;
    } catch(...) {
        // If we cannot set an error, terminate process - this is a critical condition
        std::terminate();
    }
}
