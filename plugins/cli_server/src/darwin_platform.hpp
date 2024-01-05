#pragma once
#include "platform.hpp"

class DarwinPlatform : public Platform {
    void createUser(std::string user) override {
        std::ignore = runCmd("user=" + user + "\n");
        auto res = runCmd(
            "$(($(dscl . -list /Users UniqueID | awk '{print $2}' | sort -ug | tail -1)+1))\n");
        auto uniqueId = res.output;
        std::ignore = runCmd("dscl . -create /Users/" + user + "\n");
        std::ignore = runCmd("dscl . -create /Users/" + user + " UserShell /bin/bash\n");
        std::ignore = runCmd("dscl . -create /Users/" + user + " UniqueID " + uniqueId + "\n");
        std::ignore =
            runCmd("dscl . -create /Users/" + user + " PrimaryGroupID " + uniqueId + "\n");
    }
    void createGroup(std::string group) override {
        auto res =
            runCmd("$(($(dscl . -list /Groups gid | awk '{print $2}' | sort -ug | tail -1)+1))\n");
        auto gid = res.output;
        std::ignore = runCmd("dscl . -create /Groups/" + group + "\n");
        std::ignore = runCmd("dscl . -append /Groups/" + group + "PrimaryGroupID " + gid + "\n");
    }
    void addUserToGroup(std::string user, std::string group) override {
        std::ignore = runCmd("dscl . -append /Groups/" + group + " GroupMembership " + user + "\n");
    }
};
