#pragma once
#include "errors/errors.hpp"
#include <cpp_api.hpp>
#include <functional>

namespace apiImpl {
    template<typename Func, typename... Args>
    inline uint32_t catchErrorToKind(const Func &f, Args &&...args) noexcept {
        try {
            std::invoke(f, std::forward<Args>(args)...);
            return 0;
        } catch(...) {
            return errors::Error::of(std::current_exception()).toThreadLastError();
        }
    }

    inline void setBool(ggapiBool *pBool, bool test) noexcept {
        *pBool = test ? 1 : 0;
    }

} // namespace apiImpl
