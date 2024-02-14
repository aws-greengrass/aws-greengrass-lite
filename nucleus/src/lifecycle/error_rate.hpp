#pragma once

#include <algorithm>
#include <array>
#include <chrono>

constexpr int MAXERRORS = 3;

class errorRate {
public:
    errorRate() {
        clearErrors();
    }
    typedef std::array<std::chrono::steady_clock::time_point, MAXERRORS> history_t;
    using clock = std::chrono::steady_clock;

    void newError() {
        std::rotate(_history.begin(), _history.begin() + 1, _history.end()); // rotate to the left
        _history.back() = clock::now();
    }

    bool isBroken() {
        if(_history.front() == clock::time_point()) {
            return false;
        }
        auto age = _history.back() - _history.front();
        using namespace std::chrono_literals;
        return age < 1h;
    }

    constexpr void clearErrors() {
        _history.fill(clock::time_point());
    }

private:
    history_t _history;
};
