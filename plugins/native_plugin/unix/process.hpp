#pragma once

#include "file_descriptor.hpp"
#include <chrono>

namespace ipc {

    using PosixMilliseconds = std::chrono::duration<int, std::milli>;
    class Process;

    struct UserInfo {
        uid_t uid;
        gid_t gid;
    };

    UserInfo getUserInfo(
        std::string_view username, std::optional<std::string_view> groupname = std::nullopt);

    void setUserInfo(UserInfo user);

    class ProcessImpl {
    public:
        int _pidfd;
        FileDescriptor _out;
        FileDescriptor _err;
        UserInfo _user;

        static std::optional<int> poll(Process &p, std::optional<PosixMilliseconds> timeout);
        static int runToCompletion(Process &p);
    };

} // namespace ipc
