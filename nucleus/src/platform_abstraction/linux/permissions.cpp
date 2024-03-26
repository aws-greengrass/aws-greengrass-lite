#include "permissions.hpp"

#include "syscall.hpp"

#include <stdexcept>
#include <system_error>

#include <string>
#include <vector>

#include <grp.h>
#include <pwd.h>
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

    void setUserInfo(UserInfo user) noexcept {
        // if either fail, aborting is safest to avoid creating
        // a privileged process
        if(sys_setgid(user.gid) == -1) {
            perror("setgid: Failed to set to the configured group");
            std::abort();
        } else if(sys_setuid(user.uid) == -1) {
            perror("setuid: Failed to set to the configured user");
            std::abort();
        }
    }

} // namespace ipc
