#pragma once
#include <string>
#ifdef _WIN32
#include <windows.h>
template<typename... Args>
static inline auto popen(Args &&...args) -> decltype(_popen(std::forward<Args>(args)...)) {
    return _popen(std::forward<Args>(args)...);
}
template<typename... Args>
static inline auto pclose(Args &&...args) -> decltype(_pclose(std::forward<Args>(args)...)) {
    return _pclose(std::forward<Args>(args)...);
}
#else
#include <cstdlib>
#endif

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
