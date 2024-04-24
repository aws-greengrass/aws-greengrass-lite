#include "span.hpp"
#include <cerrno>
#include <cstdlib>
#include <gg_pal/process.hpp>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <utility>

extern "C" {
#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
}

// This declaration is required by Posix
// NOLINTNEXTLINE(readability-redundant-declaration,*-avoid-non-const-global-variables)
extern char **environ;

namespace gg_pal {

    // File descriptors need to be wrapped so that they will get closed by the destructor in case of
    // early exit or exception
    struct FileDescriptor {
        int fd{-1};

        constexpr FileDescriptor() noexcept = default;
        explicit constexpr FileDescriptor(int fd) noexcept : fd{fd} {
        }

        [[nodiscard]] explicit constexpr operator bool() const noexcept {
            return fd >= 0;
        }

        FileDescriptor &operator=(const FileDescriptor &) = delete;
        FileDescriptor(const FileDescriptor &) = delete;

        FileDescriptor(FileDescriptor &&other) noexcept : fd(other.release()) {
        }

        FileDescriptor &operator=(FileDescriptor &&other) noexcept {
            close();
            fd = other.release();
            return *this;
        }

        ~FileDescriptor() noexcept {
            close();
        }

        [[nodiscard]] int release() noexcept {
            return std::exchange(fd, -1);
        }

        void close() noexcept {
            if(int old = std::exchange(fd, -1); old != -1) {
                if(::close(old) == -1) {
                    perror("close");
                }
            }
        }

        void moveTo(int newFd) {
            if(::dup2(fd, newFd) == -1) {
                throw std::system_error(errno, std::generic_category());
            }
            close();
        }

        [[nodiscard]] util::Span<char> read(util::Span<char> buffer) const {
            while(true) {
                ssize_t bytesRead = ::read(fd, buffer.data(), buffer.size_bytes());
                if(bytesRead < 0) {
                    if(errno == EINTR) {
                        continue;
                    }
                    perror("read");
                    throw std::system_error(errno, std::generic_category());
                } else {
                    return buffer.first(bytesRead);
                }
            }
        }
    };

    struct Pipe {
        FileDescriptor output;
        FileDescriptor input;

        explicit Pipe(std::array<int, 2> fd) noexcept : output{fd[0]}, input{fd[1]} {
        }

        Pipe()
            : Pipe{[]() {
                  std::array<int, 2> fds{};
                  if(pipe(fds.data()) == -1) {
                      throw std::system_error(errno, std::generic_category());
                  }
                  return fds;
              }()} {
        }
    };

    static std::tuple<uid_t, gid_t> getUserInfo(
        std::string_view username, std::optional<std::string_view> groupname = std::nullopt) {
        constexpr auto defaultBufferSize = 0x0FFF;

        auto bufferSize = sysconf(_SC_GETPW_R_SIZE_MAX);
        if(bufferSize == -1) {
            bufferSize = defaultBufferSize;
        }
        std::vector<char> buffer(bufferSize, '\0');

        struct passwd pw {};

        {
            struct passwd *result{};
            auto err = getpwnam_r(username.data(), &pw, buffer.data(), buffer.size(), &result);
            if(result == nullptr) {
                using namespace std::string_literals;
                // not found
                if(err == 0) {
                    throw std::invalid_argument("Unknown user "s + std::string{username});
                }
                // error, may be permissions-based.
                else {
                    errno = err;
                    throw std::system_error(errno, std::generic_category());
                }
            }
        }

        if(!groupname.has_value()) {
            return {pw.pw_uid, pw.pw_gid};
        }

        bufferSize = sysconf(_SC_GETGR_R_SIZE_MAX);
        if(bufferSize != -1) {
            buffer.resize(bufferSize);
        }

        struct group gr {};

        {
            struct group *result{};
            auto err = getgrnam_r(groupname->data(), &gr, buffer.data(), buffer.size(), &result);
            if(result == nullptr) {
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
        }

        return {pw.pw_uid, gr.gr_gid};
    }

    static void fdReaderFn(
        OutputCallback stdoutCallback,
        OutputCallback stderrCallback,
        FileDescriptor outfd,
        FileDescriptor errfd) {
        static constexpr size_t defaultBufferSize = 0xFFF;

        try {
            std::array<pollfd, 2> fds{};
            fds[0].fd = outfd.fd;
            fds[0].events = POLLIN | POLLPRI;
            fds[1].fd = errfd.fd;
            fds[1].events = POLLIN | POLLPRI;

            std::array<char, defaultBufferSize> buffer{};

            static constexpr int pollTimeoutMs = 10000;

            while(true) {
                int result = poll(fds.data(), fds.size(), pollTimeoutMs);

                if(result == -1) {
                    if((errno == EINTR) || (errno == EAGAIN)) {
                        continue;
                    }

                    perror("poll");
                    return;
                }

                bool emptyRead = false;
                // Regular or high priority data available on stdout
                if((fds[0].revents & POLLIN) || (fds[0].revents & POLLPRI)) {
                    auto readVal = outfd.read(buffer);
                    stdoutCallback(util::as_bytes(readVal));
                    if(readVal.empty()) {
                        emptyRead = true;
                    }
                }
                // No data read, conn closed, or error
                if(emptyRead || (fds[0].revents & POLLHUP) || (fds[0].revents & POLLERR)) {
                    outfd.close();
                    fds[0].fd = -1;
                }

                emptyRead = false;
                // Regular or high priority data available on stderr
                if((fds[1].revents & POLLIN) || (fds[1].revents & POLLPRI)) {
                    auto readVal = errfd.read(buffer);
                    stderrCallback(util::as_bytes(readVal));
                    if(readVal.empty()) {
                        emptyRead = true;
                    }
                }
                // No data read, conn closed, or error
                if(emptyRead || (fds[1].revents & POLLHUP) || (fds[1].revents & POLLERR)) {
                    errfd.close();
                    fds[1].fd = -1;
                }
            };
        } catch(...) {
            // TODO: Log exception
            // TODO: Call callback to allow caller to handle failure.
        }
    }

    static void retHandlerFn(CompletionCallback onComplete, pid_t pid) {
        try {
            while(true) {
                int stat;
                pid_t ret = waitpid(pid, &stat, 0);

                if(ret == -1) {
                    if(errno == EINTR) {
                        continue;
                    }
                    // TODO: Log error
                    perror("waitpid");
                    return;
                }

                if(WIFEXITED(stat)) {
                    onComplete(WEXITSTATUS(stat));
                    break;
                }
                if(WIFSIGNALED(stat)) {
                    onComplete(ENOENT);
                    break;
                }
            }
        } catch(...) {
            // TODO: Log exception
            // TODO: Call callback to allow caller to handle failure.
        }
    }

    Process::Process(
        std::string file,
        std::vector<std::string> args,
        std::filesystem::path workingDir,
        EnvironmentMap environment,
        std::optional<std::string> user,
        std::optional<std::string> group,
        OutputCallback stdoutCallback,
        OutputCallback stderrCallback,
        CompletionCallback onComplete)
        : _data{[&]() -> pid_t {
              Pipe outPipe{};
              Pipe errPipe{};

              auto userInfo = [&]() -> std::optional<std::tuple<uid_t, gid_t>> {
                  if(user.has_value() && !user.value().empty()) {
                      if(group.has_value() && !group.value().empty()) {
                          return getUserInfo(*user, *group);
                      } else {
                          return getUserInfo(*user);
                      }
                  }
                  return std::nullopt;
              }();

              // null terminated as of C++11
              char *filePtr = file.data();

              std::vector<std::string> envVec(environment.size());
              std::transform(
                  environment.begin(), environment.end(), envVec.begin(), [](auto &&pair) {
                      if(pair.second.has_value()) {
                          return pair.first + "=" + *pair.second;
                      } else {
                          return pair.first;
                      }
                  });

              // null terminated as of C++11
              auto toCharP = [](std::string &str) { return str.data(); };

              std::vector<char *> envp(envVec.size() + 1);
              std::transform(envVec.begin(), envVec.end(), envp.begin(), toCharP);

              std::vector<char *> argsp(args.size() + 2);
              argsp[0] = filePtr;
              std::transform(args.begin(), args.end(), std::next(argsp.begin()), toCharP);

              // Note: all memory allocation for the child process must be performed before forking

              pid_t pid = fork();

              switch(pid) {
                  case -1: {
                      // parent, on error
                      // TODO: Log error
                      perror("fork");
                      throw std::system_error(errno, std::generic_category());
                  }

                  case 0: {
                      // child, runs process

                      // At this point, child should be extremely careful which APIs they call;
                      // async-signal-safe to be safest

                      // set pgid to current child pid so all decendants are reaped when
                      // SIGKILL/SIGTERM is received
                      setpgid(0, 0);

                      if(userInfo) {
                          if(setuid(std::get<0>(*userInfo)) == -1) {
                              perror("setuid: Failed to set to the configured group");
                              std::abort();
                          } else if(setgid(std::get<1>(*userInfo)) == -1) {
                              perror("setgid: Failed to set to the configured user");
                              std::abort();
                          }
                      }

                      if(chdir(workingDir.c_str()) == -1) {
                          // TODO: Log error
                          perror("chdir");
                      }

                      // close stdin
                      FileDescriptor{STDIN_FILENO}.close();

                      // pipe program output to parent process
                      outPipe.output.close();
                      errPipe.output.close();
                      outPipe.input.moveTo(STDOUT_FILENO);
                      errPipe.input.moveTo(STDERR_FILENO);

                      environ = envp.data();

                      std::ignore = execvp(filePtr, argsp.data());

                      // only reachable if exec fails
                      // TODO: Log error
                      perror("execvp");
                      // SECURITY-TODO: log permissions error
                      if(errno == EPERM || errno == EACCES) {
                      }
                      std::abort();
                  }

                  default: {
                      static constexpr size_t defaultBufferSize = 0xFFF;

                      // While we could use a shared thread to poll on the file descriptors for all
                      // Processes' outputs, this would incur extra complexity. We'd need to manage
                      // the state of the global polling thread, and keep it synchronized with
                      // starting threads. We'd need a mutex to guard data access from multiple
                      // threads, and two vectors to store the state. Since poll blocks and mutates
                      // its array, new values will have to be written elsewhere for the polling
                      // thread to merge into its main array between poll calls. The thread will
                      // also need to be interrupted so that it can add the new items. Cleanup will
                      // be similar. Poll also requires scanning the array for changes, so does not
                      // scale well, though this is unlikely to matter at our scale. Similarly at
                      // our scale, a thread per Process will incur around 2 pages of memory use,
                      // which is small and thus the solution used here to keep the implementation
                      // simple.
                      std::thread fdReader{
                          fdReaderFn,
                          stdoutCallback,
                          stderrCallback,
                          std::move(outPipe.output),
                          std::move(errPipe.output)};
                      fdReader.detach();

                      // We can't use SIGCHLD to catch child thread exits, as signal handling is
                      // process-wide, and doing so would conflict with nucleus and other plugins.
                      // We could potentially have a separate process, with a queue for offloading
                      // forking children, and a queue to return results, though that would make
                      // debugging difficult compared to just a watcher thread per child as below.
                      std::thread retHandler{retHandlerFn, onComplete, pid};
                      retHandler.detach();
                  }
              }

              return pid;
          }()} {
    }

    Process::operator bool() const noexcept {
        return _data != 0;
    }

    void Process::stop() const {
        if(::kill(-_data, SIGTERM) < 0) {
            throw std::system_error{errno, std::generic_category()};
        }
    }

    void Process::kill() const {
        if(::kill(-_data, SIGKILL) < 0) {
            throw std::system_error{errno, std::generic_category()};
        }
    }

} // namespace gg_pal
