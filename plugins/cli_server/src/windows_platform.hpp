#pragma once
#include "platform.hpp"

class WindowsPlatform : public Platform {
    void createUser(std::string) override;
    void createGroup(std::string) override;
    void addUserToGroup(std::string, std::string) override;
};
