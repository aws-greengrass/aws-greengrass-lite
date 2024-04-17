#include "pipe.hpp"
#include "rlimits.hpp"
#include "syscall.hpp"
#include <csignal>
#include <gg_pal/abstract_process.hpp>
#include <gg_pal/file_descriptor.hpp>
#include <gg_pal/process.hpp>
#include <gg_pal/startable.hpp>
#include <memory>
#include <string_view>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>

namespace ipc {

    inline constexpr auto defaultBufferSize = 0x0FFF;

    std::unique_ptr<Process> Startable::start(
        std::string_view command, util::Span<char *> argv, util::Span<char *> envp) const {

        // prepare to capture child process output
        Pipe outPipe{};
        Pipe errPipe{};

        // Prepare to alter user permissions
        UserInfo user{};
        if(_user.has_value() && _user.value() != "") {
            if(_group.has_value() && _group.value() != "") {
                user = getUserInfo(*_user, *_group);
            } else {
                user = getUserInfo(*_user);
            }
        }

        // Note: all memory allocation for the child process must be performed before forking

        int pidfdOut;

        clone_args cloneArgs{};
        cloneArgs.flags = CLONE_PIDFD;
        // NOLINTNEXTLINE(*-pro-type-reinterpret-cast) Linux API compatibility
        cloneArgs.pidfd = reinterpret_cast<__aligned_u64>(&pidfdOut);
        cloneArgs.exit_signal = SIGCHLD;

        auto pid = sys_clone3(&cloneArgs);

        switch(pid) {
            // parent, on error
            case -1:
                perror("clone3");
                throw std::system_error(errno, std::generic_category());

            // child, runs process
            case 0: {
                // At this point, child should be extremely careful which APIs they call;
                // async-signal-safe to be safest

                // child process may be using select, which requires fds <= 1024
                resetFdLimit();

                // set pgid to current child pid so all decendants are reaped when SIGKILL/SIGTERM
                // is received
                // std::ignore = setsid();
                setpgid(0, 0);

                // close stdin
                FileDescriptor{STDIN_FILENO}.close();

                // pipe program output to parent process
                outPipe.input().duplicate(STDOUT_FILENO);
                errPipe.input().duplicate(STDERR_FILENO);
                std::ignore = outPipe.input().release();
                std::ignore = errPipe.input().release();
                outPipe.output().close();
                errPipe.output().close();

                setUserInfo(user);

                if(_workingDir.has_value()) {
                    if(chdir(_workingDir->c_str()) == -1) {
                        perror("chdir");
                    }
                }

                std::ignore = execvpe(_command.c_str(), argv.data(), envp.data());
                // only reachable if exec fails
                perror("execvpe");
                // SECURITY-TODO: log permissions error
                if(errno == EPERM || errno == EACCES) {
                }
                std::abort();
            }

            // parent process, PID is child process
            default: {
                FileDescriptor pidfd{pidfdOut};
                if(!pidfd) {
                    // Most likely: out of file descriptors
                    throw std::system_error(std::error_code{EMFILE, std::generic_category()});
                }

                outPipe.input().close();
                errPipe.input().close();

                auto process = std::make_unique<Process>();
                process->setPidFd(std::move(pidfd))
                    .setPid(pid)
                    .setOut(std::move(outPipe.output()))
                    .setErr(std::move(errPipe.output()))
                    .setCompletionHandler(_completeHandler.value_or([](auto &&...) {}))
                    .setErrHandler(_errHandler.value_or([](auto &&...) {}))
                    .setOutHandler(_outHandler.value_or([](auto &&...) {}))
                    .setTimeout(_timeout.value_or(std::chrono::steady_clock::time_point::min()));
                return process;
            }
        }
    }

} // namespace ipc
