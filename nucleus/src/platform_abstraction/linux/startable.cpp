#include "platform_abstraction/startable.hpp"
#include "file_descriptor.hpp"
#include "pipe.hpp"
#include "platform_abstraction/abstract_process.hpp"
#include "process.hpp"
#include "rlimits.hpp"
#include "syscall.hpp"
#include <csignal>
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
        // uid setting only works as root or with root-like permissions
        // TODO: investigate. Probably need a root setuid daemon...
        // or just ignore this step
        if(getgid() == 0 && getuid() == 0) {
            if(_user.has_value()) {
                if(_group.has_value()) {
                    getUserInfo(*_user, *_group);
                } else {
                    getUserInfo(*_user);
                }
            }
        }

        // Note: all memory allocation for the child process must be performed before forking

        int pidfdOut;

        clone_args clargs{
            .flags = CLONE_PIDFD,
            // NOLINTNEXTLINE(*-pro-type-reinterpret-cast) Linux API compatibility
            .pidfd = reinterpret_cast<__aligned_u64>(&pidfdOut),
            .exit_signal = SIGCHLD,
        };

        auto pid = sys_clone3(&clargs);

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

                // create a session so all decendants are reaped when SIGKILL/SIGTERM is received
                std::ignore = setsid();

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
                    .setOut(std::move(outPipe.output()))
                    .setErr(std::move(errPipe.output()))
                    .setCompletionHandler(_completeHandler.value_or([](auto &&...) {}))
                    .setErrHandler(_errHandler.value_or([](auto &&...) {}))
                    .setOutHandler(_outHandler.value_or([](auto &&...) {}));
                return process;
            }
        }
    }

} // namespace ipc
