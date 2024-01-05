#pragma once
#include "platform.hpp"
#include <pwd.h>
#include <stdlib.h>
#include <unistd.h>

struct UnixUser {
    long long int primaryGid;
    std::string principalName;
    std::string principalIdentifier;

    bool isSuperUser() {
        return std::stoi(principalIdentifier) == 0;
    }
};

class UnixPlatform : public Platform {
public:
    void createUser(std::string user) override {
        std::ignore = runCmd("useradd -r -m " + user);
    }
    void createGroup(std::string group) override {
        std::ignore = runCmd("groupadd -r " + group);
    }
    void addUserToGroup(std::string user, std::string group) override {
        std::ignore = runCmd("usermod -a -G " + group + " " + user);
    }

    bool userExists(std::string user) const {
        auto res = runCmd("id -u " + user);
        return res.returnCode == 0;
    }

    UnixUser lookupCurrentUser() {
        auto uid = geteuid();
        auto pw = getpwuid(uid);
        return lookupUser(pw->pw_name);
    }

    UnixUser lookupUser(std::string user) const {
        UnixUser attribs;
        bool isNumeric =
            std::find_if(user.begin(), user.end(), [](auto c) { return !std::isdigit(c); })
            == user.end();
        if(isNumeric) {
            attribs.principalIdentifier = user;
        }
        attribs.principalName = user;

        // check if user exists
        if(userExists(user)) {
            // get user group id
            auto res = runCmd("id -g " + user);
            if(!res.returnCode) {
                res.output.pop_back();
                attribs.primaryGid = std::stoi(res.output);
                if(isNumeric) {
                    attribs.principalName = res.output;
                } else {
                    attribs.principalIdentifier = res.output;
                }
            } else {
                throw std::runtime_error("User is not associated with any group");
            }
        } else {
            throw std::runtime_error("User do not exist: " + user);
        }
        return attribs;
    }
};
