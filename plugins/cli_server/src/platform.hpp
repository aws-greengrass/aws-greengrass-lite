#pragma once
#include <cstdlib>
#include <string>

struct CmdResult {
    std::string output;
    int returnCode;

    CmdResult(std::string out, int code) : output(std::move(out)), returnCode(code) {
    }
};

class Platform {
protected:
    Platform() noexcept = default;

public:
    Platform(const Platform &) = delete;
    Platform(Platform &&) noexcept = delete;
    Platform &operator=(const Platform &) = delete;
    Platform &operator=(Platform &&) noexcept = delete;

    virtual ~Platform() noexcept = default;
    [[nodiscard]] virtual CmdResult runCmd(const std::string &cmd) const = 0;
    virtual void createUser(const std::string &) = 0;
    virtual void createGroup(const std::string &) = 0;
    virtual void addUserToGroup(const std::string &, const std::string &) = 0;
};
