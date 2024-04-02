#pragma once

#include <cerrno>

namespace ipc {
    inline constexpr bool isNonBlockingError(int _errno) noexcept {
        if constexpr(EAGAIN == EWOULDBLOCK) {
            return _errno == EWOULDBLOCK;
        } else {
            return _errno == EWOULDBLOCK || _errno == EAGAIN;
        }
    }
} // namespace ipc
