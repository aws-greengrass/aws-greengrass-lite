
#include "../startable.hpp"
#include "util.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>

#if defined(__APPLE__) || defined(__unix__)
#include <grp.h>
#endif
#ifdef __APPLE__
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#endif

namespace ipc {

    class FileDescriptor {
        int _fd{};

    public:
        explicit constexpr FileDescriptor(int fd) : _fd{fd} {
            if(_fd == -1) {
                throw std::invalid_argument("fd must not be -1");
            }
        }

        constexpr FileDescriptor() noexcept = default;

        FileDescriptor &operator=(FileDescriptor &) = delete;
        FileDescriptor(FileDescriptor &) = delete;

        FileDescriptor(FileDescriptor &&other) noexcept : _fd(std::exchange(other._fd, 0)) {
        }
        FileDescriptor &operator=(FileDescriptor &&other) noexcept {
            _fd = std::exchange(other._fd, 0);
            return *this;
        }

        ~FileDescriptor() noexcept {
            close();
        }

        void close() noexcept {
            if(_fd) {
                if(::close(std::exchange(_fd, 0)) == -1) {
                    perror("close");
                }
            }
        }

        void duplicate(int fd) const {
            if(dup2(_fd, fd) == -1) {
                throw std::system_error(errno, std::generic_category());
            }
        }

        [[nodiscard]] int descriptor() const noexcept {
            return _fd;
        }

        template<class T>
        auto write(util::Span<T> buffer) noexcept {
            return ::write(_fd, util::as_bytes(buffer).data(), buffer.size_bytes());
        }

        template<class T>
        auto read(util::Span<T> buffer) noexcept {
            return ::read(_fd, util::as_writeable_bytes(buffer).data(), buffer.size_bytes());
        }
    };

    class Pipe {
        FileDescriptor _output;
        FileDescriptor _input;

    public:
        Pipe() {
            std::array<int, 2> fds{};
            if(pipe(fds.data()) == -1) {
                throw std::system_error(errno, std::generic_category());
            }
            for(auto fs : fds) {
                auto flags = fcntl(fs, F_GETFL, 0);
                if(flags == -1) {
                    throw std::system_error(errno, std::generic_category());
                } else {
                    auto error = fcntl(fs, F_SETFL, O_NONBLOCK);
                }
            }
            _output = FileDescriptor{fds[0]};
            _input = FileDescriptor{fds[1]};
        }

        FileDescriptor &input() noexcept {
            return _input;
        }

        FileDescriptor &output() noexcept {
            return _output;
        }

        template<class T>
        auto write(util::Span<T> buffer) noexcept {
            return _input.write(util::as_bytes(buffer));
        }

        template<class T>
        auto read(util::Span<T> buffer) noexcept {
            return _output.read(util::as_writeable_bytes(buffer));
        }
    };

    struct UserInfo {
        uid_t uid;
        gid_t gid;
    };

    static UserInfo getUserInfo(
        std::string_view username, std::optional<std::string_view> groupname = std::nullopt) {
        static constexpr auto defaultBufferSize = 0x3FFF;
        auto bufferSize = sysconf(_SC_GETPW_R_SIZE_MAX);
        if(bufferSize == -1) {
            bufferSize = defaultBufferSize;
        }

        std::vector<char> buffer(bufferSize, '\0');
        struct passwd pw {};
        struct passwd *result{};

        auto err = getpwnam_r(username.data(), &pw, buffer.data(), buffer.size(), &result);
        if(result == nullptr) {
            using namespace std::string_literals;
            // not found
            if(err == 0) {
                throw std::invalid_argument("Unknown user "s + std::string{username});
            }
            // error, may be permissions-based
            else {
                errno = err;
                throw std::system_error(errno, std::generic_category());
            }
        }

        if(!groupname.has_value()) {
            return {pw.pw_uid, pw.pw_gid};
        }

        bufferSize = sysconf(_SC_GETGR_R_SIZE_MAX);
        if(bufferSize > buffer.size()) {
            buffer.resize(bufferSize);
        }

        struct group gr {};
        struct group *res{};
        err = getgrnam_r(groupname->data(), &gr, buffer.data(), buffer.size(), &res);

        if(res == nullptr) {
            using namespace std::string_literals;
            // not found
            if(err == 0) {
                throw std::invalid_argument("Unknown group "s + std::string{username});
            }
            // error, may be permissions-based
            else {
                errno = err;
                throw std::system_error(errno, std::generic_category());
            }
        }

        return {pw.pw_uid, gr.gr_gid};
    }

    static void setUserInfo(UserInfo user) noexcept {
        // Cannot set uid or gid to root
        // So, using them as sentinel values here is OK
        if(user.uid == 0 || user.gid == 0) {
            return;
        }

        if(setgid(user.gid) == -1) {
            perror("setgid");
            std::abort();
        } else if(setuid(user.uid) == -1) {
            perror("setuid");
            std::abort();
        }
    }

    void Startable::Start() {
        if(_command.empty()) {
            return;
        }

        // Add SVCUID and IPC socket path
        // TODO: Get token and path from IPC plugin
        std::string token = "SVCUID=67TPFT1C5SNVYZ4T";
        std::string socket =
            "AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT=/tmp/gglite-ipc.socket";
        auto environment = GetEnvironment();
        // args and environment must each be a null-terminated array of pointers
        // Packed as follows: [ command | argv | nullptr | token | socket | envp | nullptr ]
        std::vector<char *> combinedArgvEnvp(
            1 + (_args.size() + 1) + 2 + (environment.size()), nullptr);
        combinedArgvEnvp.front() = _command.data();
        auto envi = std::transform(
            _args.begin(), _args.end(), std::next(combinedArgvEnvp.begin()), [](std::string &s) {
                return s.data();
            });
        // skip over the first null-terminator; this marks the start of the envp array
        ++envi;
        auto sizeEnvBegin = envi - combinedArgvEnvp.begin();
        *envi++ = token.data();
        *envi++ = socket.data();
        std::transform(
            environment.begin(), environment.end(), envi, [](std::string &s) { return s.data(); });
        for(auto env = environ; *env != nullptr; env++) {
            combinedArgvEnvp.push_back(*env);
        }
        combinedArgvEnvp.push_back(nullptr);
        // prepare to capture child process output
        Pipe outPipe{};
        Pipe errPipe{};

        UserInfo user{};
        if(_user && _group) {
            getUserInfo(*_user, *_group);
        } else if(_user) {
            getUserInfo(*_user);
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
                outPipe.output().close();
                errPipe.output().close();

                setUserInfo(user);

                char **argv = combinedArgvEnvp.data();
                char **envp = &combinedArgvEnvp[sizeEnvBegin];
                environ = envp;
                std::ignore = execvp(_command.c_str(), argv);
                // only reachable if exec fails
                perror("execvp");
                // SECURITY-TODO: log permissions error
                if(errno == EPERM || errno == EACCES) {
                }
                std::abort();
            }

            // parent process, PID is child process
            default: {
                if(!_isDetached) {
                    // TODO: add to process group so that Startable is killed when Nucleus is killed
                }

                outPipe.input().close();
                errPipe.input().close();

                std::string buffer(1000, '\0');

                using namespace std::chrono_literals;
                std::this_thread::sleep_for(1s);

                ssize_t bytesRead = outPipe.read(util::Span{buffer});
                if(bytesRead > 0) {
                    std::cout.write(buffer.c_str(), bytesRead);
                } else if(bytesRead == -1) {
                    perror("read");
                }

                bytesRead = errPipe.read(util::Span{buffer});
                if(bytesRead > 0) {
                    std::cerr.write(buffer.c_str(), bytesRead);
                } else if(bytesRead == -1) {
                    perror("read");
                }
            }
        }
    }

} // namespace ipc
