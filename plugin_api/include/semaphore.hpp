#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace util {
    /**
     * C++20 provides std::counting_semaphore
     * This is a cross-platform implementation
     * Use std::condition_variable where simple notification required
     */
    class Semaphore {
        std::atomic_int32_t _count;
        std::condition_variable _wait;
        std::mutex _mutex;

    public:
        static constexpr int32_t MAX = std::numeric_limits<int32_t>::max();

        Semaphore(const Semaphore &other) = delete;
        Semaphore(Semaphore &&other) = delete;
        Semaphore &operator=(const Semaphore &other) = delete;
        Semaphore &operator=(Semaphore &&other) = delete;
        ~Semaphore() noexcept = default;
        explicit Semaphore(int32_t initialCount) noexcept : _count(initialCount) {
        }

        bool tryAcquire() noexcept {
            for(;;) {
                int32_t count = _count.load();
                if(count == 0) {
                    return false; // would block
                }
                // compare-exchange used to ensure we never decrement 0 to -1
                // Note, it is acceptable for count to already be negative
                if(_count.compare_exchange_weak(count, count - 1)) {
                    return true;
                }
            }
        }

        void acquire() {
            for(;;) {
                if(tryAcquire()) {
                    return;
                }
                std::unique_lock guard{_mutex};
                _wait.wait(guard);
            }
        }

        template<typename Clock, typename Duration>
        bool tryAcquireUntil(const std::chrono::time_point<Clock, Duration> &abs) {
            for(;;) {
                if(tryAcquire()) {
                    return true;
                }
                std::unique_lock guard{_mutex};
                if(_wait.wait_until(guard, abs) == std::cv_status::timeout) {
                    return false;
                }
            }
        }

        template<typename Rep, typename Period>
        bool tryAcquireFor(const std::chrono::time_point<Rep, Period> &rel) {
            return tryAcquireUntil(rel + std::chrono::steady_clock::now());
        }

        void release(int32_t update = 1) {
            for(;;) {
                auto precondCount = _count.load();
                if(precondCount + update <= precondCount) {
                    throw std::logic_error("Semaphore would decrement");
                }
                // compare-exchange used to ensure pre-condition is never invalidated
                if(_count.compare_exchange_weak(precondCount, precondCount + update)) {
                    return; // succeeded to increment without wrapping
                }
            }
        }
    };
} // namespace util
