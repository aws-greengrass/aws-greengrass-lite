
#include <startable.hpp>

#include "pipe.hpp"
#include "process.hpp"

#if defined(__APPLE__) || defined(__unix__)
#include <sys/syscall.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#endif

static int pidfd_open(pid_t pid, unsigned int flags) noexcept {
    // NOLINTNEXTLINE(*-vararg) syscall is variadic
    return static_cast<int>(syscall(SYS_pidfd_open, pid, flags));
}

namespace ipc {

    inline constexpr auto defaultBufferSize = 0x0FFF;

    Process Startable::start(
        std::string_view command, util::Span<char *> argv, util::Span<char *> envp) {

        // prepare to capture child process output
        Pipe outPipe{};
        Pipe errPipe{};

        // Prepare to alter user permissions
        UserInfo user{};
        if(_user.has_value()) {
            if(_group.has_value()) {
                getUserInfo(*_user, *_group);
            } else {
                getUserInfo(*_user);
            }
        }

        // Note: all memory allocation for the child process must be performed before forking
        auto pid = fork();
        switch(pid) {
            // parent, on error
            case -1:
                throw std::system_error(errno, std::generic_category());

            // child, runs process
            case 0: {
                // At this point, child shuld be extremely careful which APIs they call;
                // async-signal-safe to be safest

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

                environ = envp.data();
                std::ignore = execvp(_command.c_str(), argv.data());
                // only reachable if exec fails
                perror("execvp");
                // SECURITY-TODO: log permissions error
                if(errno == EPERM || errno == EACCES) {
                }
                std::abort();
            }

            // parent process, PID is child process
            default: {
                return Process{
                    _command,
                    _out,
                    _err,
                    {pidfd_open(pid, 0),
                     std::move(outPipe.output()),
                     std::move(errPipe.output()),
                     user}};
            }
        }
    }

    int Process::runToCompletion() {
        if(!_impl) {
            throw std::runtime_error("No process started");
        }
        return _impl->runToCompletion(*this);
    }

} // namespace ipc
