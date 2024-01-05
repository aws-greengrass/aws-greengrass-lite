#pragma once
#include <stdlib.h>
#include <string>

struct CommandResult {
    std::string output;
    int returnCode;

    CommandResult(std::string out, int code) : output(std::move(out)), returnCode(std::move(code)) {
    }
};

class Platform {
public:
    virtual CommandResult runCmd(std::string cmd) const {
        FILE *pipe = popen(cmd.c_str(), "r");
        if(!pipe) {
            std::runtime_error("Failed to start process!");
        }
        char buffer[1024];
        std::string out;
        while(!feof(pipe)) {
            if(fgets(buffer, sizeof(buffer), pipe)) {
                out += buffer;
            }
        }
        auto returnCode = WEXITSTATUS(pclose(pipe));
        return CommandResult(out, returnCode);
    }
    virtual void createUser(std::string) = 0;
    virtual void createGroup(std::string) = 0;
    virtual void addUserToGroup(std::string, std::string) = 0;
};
