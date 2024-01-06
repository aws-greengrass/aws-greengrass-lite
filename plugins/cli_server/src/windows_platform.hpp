#pragma once
#include "platform.hpp"

class WindowsPlatform : public Platform {
    void createUser(const std::string &) override;
    void createGroup(const std::string &) override;
    void addUserToGroup(const std::string &, const std::string &) override;
};
