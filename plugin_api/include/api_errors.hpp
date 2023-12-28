#pragma once

#include "api_forwards.hpp"
#include "handles.hpp"
#include <optional>
#include <stdexcept>

namespace ggapi {

    /**
     * Exceptions in Plugins should derive from GgApiError to pass through to Nucleus.
     */
    class GgApiError : public std::runtime_error {
        Symbol _kind;

        template<typename Error>
        static Symbol typeKind() {
            static_assert(std::is_base_of_v<std::exception, Error>);
            return typeid(Error).name();
        }

    public:
        GgApiError(const GgApiError &) noexcept = default;
        GgApiError(GgApiError &&) noexcept = default;
        GgApiError &operator=(const GgApiError &) noexcept = default;
        GgApiError &operator=(GgApiError &&) noexcept = default;
        ~GgApiError() override = default;

        explicit GgApiError(
            Symbol kind = typeKind<GgApiError>(),
            const std::string &what = "Unspecified Error") noexcept
            : _kind(kind), std::runtime_error(what) {
        }

        template<typename E>
        static GgApiError of(const E &error) {
            static_assert(std::is_base_of_v<std::exception, E>);
            return GgApiError(typeKind<E>(), error.what());
        }

        [[nodiscard]] constexpr Symbol kind() const {
            return _kind;
        }

        void toThreadLastError() {
            toThreadLastError(_kind, what());
        }

        static void toThreadLastError(Symbol kind, std::string_view what) {
            ::ggapiSetError(kind.asInt(), what.data(), what.length());
        }

        static void clearThreadLastError() {
            ::ggapiSetError(0, nullptr, 0);
        }

        static std::optional<GgApiError> fromThreadLastError(bool clear = false) {
            auto lastErrorKind = ::ggapiGetErrorKind();
            if(lastErrorKind != 0) {
                Symbol sym(lastErrorKind);
                std::string copy{::ggapiGetErrorWhat()}; // before clear
                if(clear) {
                    clearThreadLastError();
                }
                return GgApiError(sym, copy);
            } else {
                return {};
            }
        }

        static bool hasThreadLastError() {
            return ::ggapiGetErrorKind() != 0;
        }

        static void throwIfThreadHasError() {
            // Wrap in fast check
            if(hasThreadLastError()) {
                // Slower path
                auto lastError = fromThreadLastError(true);
                if(lastError.has_value()) {
                    throw GgApiError(lastError.value());
                }
            }
        }
    };

    template<>
    inline GgApiError GgApiError::of<GgApiError>(const GgApiError &error) {
        return error;
    }

    //
    // Exceptions do not cross module borders - translate an exception into a thread error
    //
    template<typename T>
    inline T trapErrorReturn(const std::function<T()> &fn) noexcept {
        try {
            GgApiError::clearThreadLastError();
            if constexpr(std::is_void_v<T>) {
                fn();
            } else {
                return fn();
            }
        } catch(std::exception &e) {
            GgApiError::of(e).toThreadLastError();
        } catch(...) {
            GgApiError().toThreadLastError();
        }
        if constexpr(std::is_void_v<T>) {
            return;
        } else {
            return static_cast<T>(0);
        }
    }

    inline void callApi(const std::function<void()> &fn) {
        GgApiError::clearThreadLastError();
        fn();
        GgApiError::throwIfThreadHasError();
    }

    template<typename T>
    inline T callApiReturn(const std::function<T()> &fn) {
        if constexpr(std::is_void_v<T>) {
            callApi(fn);
        } else {
            GgApiError::clearThreadLastError();
            T v = fn();
            GgApiError::throwIfThreadHasError();
            return v;
        }
    }

    template<typename T>
    inline T callApiReturnHandle(const std::function<uint32_t()> &fn) {
        static_assert(std::is_base_of_v<ObjHandle, T>);
        return T(callApiReturn<uint32_t>(fn));
    }

    inline Symbol callApiReturnOrd(const std::function<uint32_t()> &fn) {
        return Symbol(callApiReturn<uint32_t>(fn));
    }

} // namespace ggapi
