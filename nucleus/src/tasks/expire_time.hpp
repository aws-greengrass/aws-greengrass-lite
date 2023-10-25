#pragma once

#include <chrono>
#include <ctime>

namespace tasks { //

    // this class is used for timeouts, which depends on steady_clock rather than
    // epoch assumes conversion with milliseconds
    //
    class ExpireTime {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = std::chrono::time_point<Clock>;
        using Milliseconds = std::chrono::milliseconds;
        using Seconds = std::chrono::seconds;

    private:
        TimePoint _steadyTime;
        auto static constexpr MAX{TimePoint::max()};
        auto static constexpr MIN{TimePoint::min()};

    public:
        ExpireTime(const ExpireTime &) = default;
        ExpireTime(ExpireTime &&) = default;
        ~ExpireTime() = default;
        ExpireTime &operator=(const ExpireTime &) = default;
        ExpireTime &operator=(ExpireTime &&) = default;

        explicit ExpireTime(TimePoint time) : _steadyTime{time} {
        }

        [[nodiscard]] TimePoint toTimePoint() const {
            return _steadyTime;
        }

        [[nodiscard]] uint64_t asMilliseconds() const {
            return std::chrono::duration_cast<Milliseconds>(_steadyTime.time_since_epoch()).count();
        }

        explicit operator TimePoint() const {
            return toTimePoint();
        }

        template<typename T>
        ExpireTime operator+(const T &delta) const {
            auto newTime = _steadyTime + delta;
            if(newTime < _steadyTime && delta.count() > 0) {
                newTime = MAX;
            } else if(newTime > _steadyTime && delta.count() < 0) {
                newTime = MIN;
            }
            return ExpireTime{newTime};
        }

        Milliseconds operator-(const ExpireTime &other) const {
            return std::chrono::duration_cast<Milliseconds>(_steadyTime - other._steadyTime);
        }

        [[nodiscard]] static ExpireTime infinite() {
            return ExpireTime(MAX);
        }

        [[nodiscard]] static ExpireTime unspecified() {
            return ExpireTime(MIN);
        }

        [[nodiscard]] static ExpireTime epoch() {
            return ExpireTime(MIN + Clock::duration(1));
        }

        [[nodiscard]] static ExpireTime fromNowMillis(int32_t smallDelta) {
            if(smallDelta < 0) {
                // negative means max time
                return infinite();
            }
            return fromNow(Milliseconds(smallDelta));
        }

        [[nodiscard]] static ExpireTime fromNowSecs(int32_t delta) {
            if(delta < 0) {
                // negative means max time
                return infinite();
            }
            return fromNow(Seconds(delta));
        }

        [[nodiscard]] static ExpireTime fromNow(Milliseconds delta) {
            if(delta == Milliseconds::max()) {
                return infinite();
            }
            return ExpireTime(Clock::now() + delta);
        }

        [[nodiscard]] static ExpireTime fromNow(Seconds delta) {
            if(delta == Seconds::max()) {
                return infinite();
            }
            return ExpireTime(Clock::now() + delta);
        }

        [[nodiscard]] static ExpireTime now() {
            return ExpireTime(Clock::now());
        }

        [[nodiscard]] Milliseconds remaining() const {
            return std::chrono::duration_cast<Milliseconds>(_steadyTime - Clock::now());
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
} // namespace tasks
