#pragma once

#include <chrono>
#include <type_traits>

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

        // performs an add of a positive duration to a timepoint
        // overflows saturate to infinite()
        template<class Duration>
        [[nodiscard]] constexpr ExpireTime checkedPositiveAdd(const Duration &delta
        ) const noexcept {
            if(MAX - delta < _steadyTime) {
                return infinite();
            }
            return ExpireTime{_steadyTime + delta};
        }

    public:
        constexpr ExpireTime(const ExpireTime &) noexcept = default;
        constexpr ExpireTime(ExpireTime &&) noexcept = default;
        ~ExpireTime() noexcept = default;
        constexpr ExpireTime &operator=(const ExpireTime &) noexcept = default;
        constexpr ExpireTime &operator=(ExpireTime &&) noexcept = default;

        explicit constexpr ExpireTime(TimePoint time) noexcept : _steadyTime{time} {
        }

        [[nodiscard]] constexpr TimePoint toTimePoint() const noexcept {
            return _steadyTime;
        }

        [[nodiscard]] inline uint64_t asMilliseconds() const {
            return asCount<uint64_t, std::milli>();
        }

        template<class Rep = TimePoint::rep, class Period = TimePoint::period>
        [[nodiscard]] Rep asCount() const {
            using TargetDuration = typename std::chrono::duration<Rep, Period>;
            return std::chrono::duration_cast<TargetDuration>(_steadyTime.time_since_epoch())
                .count();
        }

        explicit constexpr operator TimePoint() const noexcept {
            return toTimePoint();
        }

        // adds a delta to an existing time
        // overflows saturate to maximal values (infinite() and unspecified())
        template<class Rep, class Period>
        constexpr ExpireTime operator+(const std::chrono::duration<Rep, Period> &delta
        ) const noexcept {
            using SourceDuration = typename std::chrono::duration<Rep, Period>;

            if constexpr(std::is_signed_v<Rep>) {
                if(delta < SourceDuration::zero()) {
                    if(_steadyTime < MIN - delta) {
                        return unspecified();
                    }
                    return ExpireTime{_steadyTime + delta};
                }
            }

            return checkedPositiveAdd(delta);
        }

        // TODO: unused and subtraction may overflow
        constexpr Milliseconds operator-(const ExpireTime &other) const {
            return std::chrono::duration_cast<Milliseconds>(_steadyTime - other._steadyTime);
        }

        [[nodiscard]] static constexpr ExpireTime infinite() noexcept {
            return ExpireTime{MAX};
        }

        [[nodiscard]] static constexpr ExpireTime unspecified() noexcept {
            return ExpireTime{MIN};
        }

        [[nodiscard]] static constexpr ExpireTime epoch() noexcept {
            return ExpireTime{MIN + Clock::duration{1}};
        }

        template<class Rep, class Period>
        [[nodiscard]] static ExpireTime fromNow(std::chrono::duration<Rep, Period> delta) noexcept {
            using TargetDuration = typename std::chrono::duration<Rep, Period>;
            if constexpr(std::is_signed_v<Rep>) {
                if(delta < TargetDuration::zero()) {
                    // negative delta means max time
                    return infinite();
                }
            }

            return ExpireTime::now().checkedPositiveAdd(delta);
        }

        // for converting durations received from cross-plugin API calls
        [[nodiscard]] inline static ExpireTime fromNowMillis(Milliseconds::rep milliseconds
        ) noexcept {
            return fromNow(Milliseconds{milliseconds});
        }

        [[nodiscard]] static ExpireTime now() noexcept {
            return ExpireTime{Clock::now()};
        }

        // TODO: unused and also subtraction may overflow
        template<class Duration = Milliseconds>
        [[nodiscard]] Duration remaining() const {
            return std::chrono::duration_cast<Duration>(_steadyTime - Clock::now());
        }

        [[nodiscard]] friend constexpr bool operator==(
            const ExpireTime &a, const ExpireTime &b
        ) noexcept {
            return a._steadyTime == b._steadyTime;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const ExpireTime &a, const ExpireTime &b
        ) noexcept {
            return !(a == b);
        }

        [[nodiscard]] friend constexpr bool operator<(
            const ExpireTime &a, const ExpireTime &b
        ) noexcept {
            return a._steadyTime < b._steadyTime;
        }

        [[nodiscard]] friend constexpr bool operator<=(
            const ExpireTime &a, const ExpireTime &b
        ) noexcept {
            return !(b < a);
        }

        [[nodiscard]] friend constexpr bool operator>(
            const ExpireTime &a, const ExpireTime &b
        ) noexcept {
            return b < a;
        }

        [[nodiscard]] friend constexpr bool operator>=(
            const ExpireTime &a, const ExpireTime &b
        ) noexcept {
            return !(a < b);
        }
    };
} // namespace tasks
