#pragma once
#include "platform.hpp"

#if __unix__ || __APPLE__
#include <pwd.h>
#endif
#include <unistd.h>

struct UnixUser {
    long long int primaryGid;
    std::string principalName;
    std::string principalIdentifier;

    [[nodiscard]] bool isSuperUser() const {
        return std::stoi(principalIdentifier) == 0;
    }
};

class UnixPlatform : public Platform {
public:
    void createUser(const std::string &username) override {
        std::ignore = runCmd("useradd -r -m " + username);
    }
    void createGroup(const std::string &group) override {
        std::ignore = runCmd("groupadd -r " + group);
    }
    void addUserToGroup(const std::string &username, const std::string &group) override {
        std::ignore = runCmd("usermod -a -G " + group + " " + username);
    }

    [[nodiscard]] bool userExists(const std::string &username) const {
        auto res = runCmd("id -u " + username);
        return res.returnCode == 0;
    }

    [[nodiscard]] UnixUser lookupCurrentUser() const {
        auto uid = geteuid();
        auto pw = getpwuid(uid);
        return lookupUser(pw->pw_name);
    }

    [[nodiscard]] UnixUser lookupUser(const std::string &username) const {
        UnixUser user;
        bool isNumeric =
            std::find_if(username.begin(), username.end(), [](auto c) { return !std::isdigit(c); })
            == username.end();
        if(isNumeric) {
            user.principalIdentifier = username;
        }
        user.principalName = username;

        // check if user exists
        if(userExists(username)) {
            // get user group id
            auto res = runCmd("id -g " + username);
            if(!res.returnCode) {
                res.output.pop_back();
                user.primaryGid = std::stoi(res.output);
                if(isNumeric) {
                    user.principalName = res.output;
                } else {
                    user.principalIdentifier = res.output;
                }
            } else {
                throw std::runtime_error("User is not associated with any group");
            }
        } else {
            throw std::runtime_error("User do not exist: " + username);
        }
        return user;
    }
};
