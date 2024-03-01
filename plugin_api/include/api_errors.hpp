#pragma once

#include "api_forwards.hpp"
#include "error_tmpl.hpp"
#include "handles.hpp"
#include <optional>
#include <stdexcept>

namespace ggapi {

    namespace traits {
        struct ErrorTraits {
            using SymbolType = ggapi::Symbol;
            static SymbolType translateKind(SymbolType symKind) noexcept {
                return symKind;
            }
            static SymbolType translateKind(ggapiErrorKind intKind) noexcept {
                return SymbolType(intKind);
            }
            static SymbolType translateKind(const std::string &strKind) noexcept {
                return {strKind};
            }
        };
    } // namespace traits

    using GgApiError = util::ErrorBase<traits::ErrorTraits>;

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
            std::ignore = GgApiError::of(e).toThreadLastError();
        } catch(...) {
            std::ignore = GgApiError::unspecified().toThreadLastError();
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
        return ObjHandle::of<T>(callApiReturn<uint32_t>(fn));
    }

    template<typename Func, typename... Args>
    inline ggapiErrorKind catchErrorToKind(Func &&f, Args &&...args) noexcept {

        try {
            std::invoke(std::forward<Func>(f), std::forward<Args>(args)...);
            return 0;
        } catch(GgApiError &err) {
            return err.toThreadLastError();
        } catch(std::exception &err) {
            return GgApiError::of<std::exception>(err).toThreadLastError();
        } catch(...) {
            return GgApiError::unspecified().toThreadLastError();
        }
    }

    template<typename Func, typename... Args>
    inline void callApiThrowError(Func &&f, Args &&...args) {

        ggapiErrorKind errKind = std::invoke(std::forward<Func>(f), std::forward<Args>(args)...);
        GgApiError::throwThreadError(errKind);
    }

    template<typename Handle, typename Func, typename... Args>
    inline Handle callHandleApiThrowError(Func &&f, Args &&...args) {

        ggapiObjHandle retHandle = 0;
        callApiThrowError(std::forward<Func>(f), std::forward<Args>(args)..., &retHandle);
        return ObjHandle::of<Handle>(retHandle);
    }

    template<typename Func, typename... Args>
    inline bool callBoolApiThrowError(Func &&f, Args &&...args) {

        ggapiBool retBool = 0;
        callApiThrowError(std::forward<Func>(f), std::forward<Args>(args)..., &retBool);
        return retBool != 0;
    }

} // namespace ggapi
