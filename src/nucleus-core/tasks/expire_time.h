#pragma once

#include <chrono>
#include <ctime>

//
// this class is used for timeouts, which depends on steady_clock rather than
// epoch assumes conversion with milliseconds
//
class ExpireTime {
private:
    std::chrono::time_point<std::chrono::steady_clock> _steadyTime;

public:
    ExpireTime(const ExpireTime &) = default;
    ExpireTime(ExpireTime &&) = default;
    ~ExpireTime() = default;
    ExpireTime &operator=(const ExpireTime &) = default;
    ExpireTime &operator=(ExpireTime &&) = default;

    explicit ExpireTime(std::chrono::time_point<std::chrono::steady_clock> time)
        : _steadyTime{time} {
    }

    explicit operator std::chrono::time_point<std::chrono::steady_clock>() const {
        return _steadyTime;
    }

    [[nodiscard]] static ExpireTime infinite() {
        return ExpireTime(std::chrono::time_point<std::chrono::steady_clock>::max());
    }

    [[nodiscard]] static ExpireTime fromNow(int32_t smallDelta) {
        if(smallDelta < 0) {
            // negative means max time
            return ExpireTime(std::chrono::time_point<std::chrono::steady_clock>::max());
        }
        return fromNow(std::chrono::milliseconds(smallDelta));
    }

    [[nodiscard]] static ExpireTime fromNow(std::chrono::milliseconds delta) {
        if(delta == std::chrono::milliseconds::max()) {
            return infinite();
        }
        return ExpireTime(std::chrono::steady_clock::now() + delta);
    }

    [[nodiscard]] static ExpireTime now() {
        return ExpireTime(std::chrono::steady_clock::now());
    }

    [[nodiscard]] std::chrono::milliseconds remaining() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            _steadyTime - std::chrono::steady_clock::now()
        );
    }

    [[nodiscard]] bool operator<(const ExpireTime &other) const {
        return _steadyTime < other._steadyTime;
    }

    [[nodiscard]] bool operator<=(const ExpireTime &other) const {
        return _steadyTime <= other._steadyTime;
    }

    [[nodiscard]] bool operator>(const ExpireTime &other) const {
        return _steadyTime > other._steadyTime;
    }

    [[nodiscard]] bool operator>=(const ExpireTime &other) const {
        return _steadyTime >= other._steadyTime;
    }

    [[nodiscard]] bool operator==(const ExpireTime &other) const {
        return _steadyTime == other._steadyTime;
    }

    [[nodiscard]] bool operator!=(const ExpireTime &other) const {
        return _steadyTime != other._steadyTime;
    }
};
