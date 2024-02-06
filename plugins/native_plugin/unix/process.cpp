#include "process.hpp"
#include "error.hpp"
#include "file_descriptor.hpp"
#include "startable.hpp"

#include "cpp_api.hpp"

#include <array>
#include <memory>
#include <string_view>
#include <system_error>
#include <type_traits>

#include <grp.h>
#include <pwd.h>
#include <sys/poll.h>
#include <unistd.h>

namespace ipc {
    namespace {
        constexpr auto defaultBufferSize = 0x0FFF;
    }

    UserInfo getUserInfo(std::string_view username, std::optional<std::string_view> groupname) {
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

    int ProcessImpl::runToCompletion(Process &p) {

        for(;;) {
            if(auto r = poll(p, {}); r.has_value()) {
                return *r;
            }
        }
    }

    void broadcastOutput(std::string_view identifier, ggapi::Channel channel, FileDescriptor &fd) {
        static constexpr size_t bufferSize = 0xFFF;
        std::array<char, bufferSize> buffer{};
        for(;;) {
            ssize_t bytesRead = fd.read(buffer);
            if(bytesRead == 0) {
                break;
            } else if(bytesRead == -1) {
                if(isNonBlockingError(errno)) {
                    break;
                } else {
                    throw std::system_error{errno, std::generic_category()};
                }
            }
            channel.write(ggapi::Struct::create()
                              .put("componentName", identifier)
                              .put("output", ggapi::Buffer{}.put(-1, util::Span{buffer})));
        }
    }

    std::optional<int> ProcessImpl::poll(Process &p, std::optional<PosixMilliseconds> timeout) {
        auto &_this_ = *p._impl;

        std::array fds{
            pollfd{_this_._out.get(), POLLHUP | POLLIN | POLLERR},
            pollfd{_this_._err.get(), POLLHUP | POLLIN | POLLERR},
            pollfd{_this_._pidfd, POLLHUP | POLLERR | POLLPRI}};

        const auto ms = timeout.value_or(PosixMilliseconds{-1}).count();

        for(;;) {
            auto err = ::poll(fds.data(), fds.size(), ms);
            if(err == -1) {
                switch(errno) {
                    case EAGAIN:
                        return {};
                    default:
                        throw std::system_error(errno, std::generic_category());
                }
            }
        }

        bool processExited = false;

        for(auto &fd : fds) {
            if(fd.fd == -1 || (fd.events | fd.revents) == 0) {
                continue;
            }
            if((fd.revents & POLLIN) != 0) {
                if(fd.fd == _this_._out.get()) {
                    broadcastOutput(p._id, p._out, _this_._out);
                } else {
                    broadcastOutput(p._id, p._err, _this_._err);
                }
            }
            if((fd.revents & (POLLHUP | POLLERR)) != 0) {
                if(fd.fd == _this_._pidfd) {
                    processExited = true;
                } else if(fd.fd == _this_._err.get()) {
                    _this_._err.close();
                    p._err.close();
                } else {
                    _this_._out.close();
                    p._out.close();
                }
                fd.fd = -1;
            }
        }
    }

} // namespace ipc
