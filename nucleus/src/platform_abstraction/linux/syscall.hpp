#pragma once

// syscalls which aren't exposed by typical Linux C-library implementations
// This file must have raw syscalls removed as they are added to newer library versions

#include <csignal>
#include <cstdint>
#include <linux/sched.h>
#include <linux/wait.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <tuple>
#include <type_traits>
#include <unistd.h>

namespace details {
    enum ByteWidth : size_t { x32 = 4, x64 = 8 };

    template<class... T>
    constexpr bool all_scalar = std::conjunction_v<std::is_scalar<T>...>;

    template<class... Args>
    std::enable_if_t</* requires */ all_scalar<Args...>, int> invokeSyscall(
        long syscallNumber, Args... args) noexcept {
        // TODO: x86_32 support. Idea: expand arguments into a tuple, using 2 32-bit values to
        // represent a 64-bit one, and passing the rest.
        // std::apply(
        //     [syscallNumber](auto... args) { return syscall(syscallNumber, args...); },
        //     std::tuple_cat(expand_arg(args)...));
        static_assert(sizeof(void *) == ByteWidth::x64);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        return static_cast<int>(syscall(syscallNumber, args...));
    }
} // namespace details

//-- pidfd family functions --//

// obtain a file descriptor that refers to a process
inline int pidfd_open(pid_t pid, unsigned int flags) noexcept {
    return details::invokeSyscall(SYS_pidfd_open, pid, flags);
}

// send a signal to a process specified by a file descriptor
inline int pidfd_send_signal(int pidfd, int sig, siginfo_t *info, unsigned int flags) noexcept {
    return details::invokeSyscall(SYS_pidfd_send_signal, pidfd, sig, info, flags);
}

// non-standard; calls waitid(3) with P_PIDFD
inline int pidfd_wait(id_t pidfd, siginfo_t *info, unsigned int flags) noexcept {
    return details::invokeSyscall(SYS_waitid, P_PIDFD, pidfd, info, flags, 0);
}

inline int sys_clone3(clone_args *info) noexcept {
    return details::invokeSyscall(SYS_clone3, info, sizeof(clone_args));
}

inline int sys_setgid(int gid) noexcept {
    return details::invokeSyscall(SYS_setgid, gid);
}

inline int sys_setuid(int uid) noexcept {
    return details::invokeSyscall(SYS_setuid, uid);
}
