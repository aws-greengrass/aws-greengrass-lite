#pragma once

#include <algorithm>
#include <array>
#include <chrono>

inline constexpr std::size_t MAX_ERRORS = 3;

class ErrorRate {
public:
    using Clock = std::chrono::steady_clock;
    using History = std::array<Clock::time_point, MAX_ERRORS>;

    void insert() noexcept {
        std::rotate(_history.begin(), _history.begin() + 1, _history.end()); // rotate to the left
        _history.back() = Clock::now();
    }

    [[nodiscard]] constexpr bool isBroken() const noexcept {
        using namespace std::chrono_literals;
        if(_history.front() == Clock::time_point{}) {
            return false;
        }
        const auto age = _history.back() - _history.front();
        return age < 1h;
    }

    constexpr explicit operator bool() const noexcept {
        return !isBroken();
    }

    constexpr void clear() noexcept {
        _history.fill(Clock::time_point{});
    }

private:
    History _history{};
};
