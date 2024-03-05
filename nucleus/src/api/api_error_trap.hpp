#pragma once
#include "errors/errors.hpp"
#include <cpp_api.hpp>
#include <functional>

namespace apiImpl {
    //template<typename Func, typename... Args>
    // inline uint32_t catchErrorToKind(const Func &f /* , Args &&...args */) noexcept {
    inline uint32_t catchErrorToKind(const std::function<void()> &f) noexcept {
        try {
            try {
                f();
                // std::invoke(f /*, std::forward<Args>(args)... */);
                return 0;
            } catch(errors::Error &err) {
                return err.toThreadLastError();
            } catch(ggapi::GgApiError &err) {
                return errors::Error::of<ggapi::GgApiError>(err).toThreadLastError();
            } catch(std::exception &err) {
                return errors::Error::of<std::exception>(err).toThreadLastError();
            } catch(...) {
                return errors::Error::unspecified().toThreadLastError();
            }
        } catch(...) {
            return 0x12345678;
        }
    }

    inline void setBool(ggapiBool *pBool, bool test) noexcept {
        *pBool = test ? 1 : 0;
    }

} // namespace apiImpl
