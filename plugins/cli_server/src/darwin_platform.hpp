#pragma once
#include "platform.hpp"

class DarwinPlatform : public Platform {
    void createUser(const std::string &username) override {
        std::ignore = runCmd("user=" + username + "\n");
        auto res = runCmd(
            "$(($(dscl . -list /Users UniqueID | awk '{print $2}' | sort -ug | tail -1)+1))\n");
        auto uniqueId = res.output;
        std::ignore = runCmd("dscl . -create /Users/" + username + "\n");
        std::ignore = runCmd("dscl . -create /Users/" + username + " UserShell /bin/bash\n");
        std::ignore = runCmd("dscl . -create /Users/" + username + " UniqueID " + uniqueId + "\n");
        std::ignore =
            runCmd("dscl . -create /Users/" + username + " PrimaryGroupID " + uniqueId + "\n");
    }
    void createGroup(const std::string &group) override {
        auto res =
            runCmd("$(($(dscl . -list /Groups gid | awk '{print $2}' | sort -ug | tail -1)+1))\n");
        auto gid = res.output;
        std::ignore = runCmd("dscl . -create /Groups/" + group + "\n");
        std::ignore = runCmd("dscl . -append /Groups/" + group + "PrimaryGroupID " + gid + "\n");
    }
    void addUserToGroup(const std::string &username, const std::string &group) override {
        std::ignore =
            runCmd("dscl . -append /Groups/" + group + " GroupMembership " + username + "\n");
    }
};
