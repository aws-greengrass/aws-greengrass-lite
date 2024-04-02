#include "process.hpp"
#include "syscall.hpp"

#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace cr = std::chrono;

namespace ipc {
    namespace {
        using namespace std::chrono_literals;
    } // namespace

    int LinuxProcess::queryReturnCode(std::error_code &ec) noexcept {
        siginfo_t info{};
        if(pidfd_wait(_pidfd.get(), &info, WEXITED | WNOHANG) < 0) {
            ec = {errno, std::generic_category()};
        } else {
            ec = {};
        }
        return info.si_status;
    }

    void LinuxProcess::close(bool force) {
        auto signal = force ? SIGKILL : SIGTERM;
        if(pidfd_send_signal(_pidfd.get(), signal, nullptr, 0) < 0) {
            throw std::system_error{errno, std::generic_category()};
        }
    }

    bool LinuxProcess::isRunning() const noexcept {
        return _pidfd.get() >= 0;
    }
} // namespace ipc
