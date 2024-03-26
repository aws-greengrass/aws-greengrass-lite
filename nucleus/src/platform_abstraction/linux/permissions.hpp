#pragma once
#include <optional>
#include <string_view>

#include <sys/types.h>

namespace ipc {
    struct UserInfo {
        uid_t uid;
        gid_t gid;
    };

    UserInfo getUserInfo(
        std::string_view username, std::optional<std::string_view> groupname = std::nullopt);

    void setUserInfo(UserInfo user) noexcept;

} // namespace ipc
