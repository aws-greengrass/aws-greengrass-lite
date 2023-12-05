#pragma once
#include "tasks/expire_time.hpp"
#include <filesystem>
#include <optional>

#ifdef _WIN32
    #define NOMINMAX
#endif

namespace config {
    //
    // GG-Java use of config timestamp can be considered to be a signed long
    // representing milliseconds since epoch. Given the special constants, it's
    // better to handle as 64-bit signed integer rather than handle all the weird
    // edge conditions.
    //
    class Timestamp {
    private:
        uint64_t _time; // since epoch

    public:
        constexpr Timestamp(const Timestamp &time) = default;
        constexpr Timestamp(Timestamp &&time) = default;

        constexpr Timestamp() : _time{0} {
        }

        explicit constexpr Timestamp(uint64_t timeMillis) : _time{timeMillis} {
        }

        template<typename T>
        explicit constexpr Timestamp(const std::chrono::time_point<T> time)
            : _time(static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch())
                    .count())) {
        }

        ~Timestamp() = default;

        static Timestamp now() {
            return Timestamp{std::chrono::system_clock::now()};
        }

        [[nodiscard]] constexpr uint64_t asMilliseconds() const noexcept {
            return _time;
        };

        constexpr bool operator==(const Timestamp &other) const noexcept {
            return _time == other._time;
        }

        constexpr bool operator!=(const Timestamp &other) const noexcept {
            return _time != other._time;
        }

        constexpr bool operator<(const Timestamp &other) const noexcept {
            return _time < other._time;
        }

        constexpr bool operator<=(const Timestamp &other) const noexcept {
            return _time <= other._time;
        }

        constexpr bool operator>(const Timestamp &other) const noexcept {
            return _time > other._time;
        }

        constexpr bool operator>=(const Timestamp &other) const noexcept {
            return _time >= other._time;
        }

        constexpr Timestamp &operator=(const Timestamp &time) = default;
        constexpr Timestamp &operator=(Timestamp &&time) = default;

        static inline constexpr Timestamp never();
        static inline constexpr Timestamp dawn();
        static inline Timestamp infinite();
        template<typename FT>
        static inline Timestamp ofFile(FT fileTime);
    };

    inline constexpr Timestamp Timestamp::never() {
        return Timestamp{0};
    }

    inline constexpr Timestamp Timestamp::dawn() {
        return Timestamp{1};
    }

    inline Timestamp Timestamp::infinite() {
        return Timestamp{std::numeric_limits<uint64_t>::max()};
    }

    template<typename FT>
    inline Timestamp Timestamp::ofFile(FT fileTime) {
        // C++17 hack, there is no universal way to convert from file-time to sys-time
        // Because 'now' is obtained twice, time is subject to slight error
        // C++20 fixes this with file_clock::to_sys
        auto sysTimeNow = std::chrono::system_clock::now();
        auto fileTimeNow = FT::clock::now();
        return Timestamp(
            sysTimeNow
            + std::chrono::duration_cast<std::chrono::milliseconds>(fileTime - fileTimeNow));
    }

} // namespace config
