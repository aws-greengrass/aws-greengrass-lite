#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <type_traits>

namespace util {

    /**
     * C++17 does not support std::atomic<std::shared_ptr<>>
     */
    template<typename T>
    class SafeSharedPtr {
        std::shared_ptr<T> _ptr;

    public:
        SafeSharedPtr() noexcept = default;
        SafeSharedPtr(const SafeSharedPtr &other) noexcept {
            store(other.load());
        }
        SafeSharedPtr(SafeSharedPtr &&other) noexcept {
            store(std::move(other));
        }
        SafeSharedPtr &operator=(const SafeSharedPtr &other) noexcept {
            store(other.load());
            return *this;
        }
        SafeSharedPtr &operator=(SafeSharedPtr &&other) noexcept {
            store(std::move(other));
            return *this;
        }
        void reset() noexcept {
            store({});
        }
        ~SafeSharedPtr() noexcept {
            reset();
        }
        // NOLINTNEXTLINE(*-explicit-constructor)
        SafeSharedPtr(const std::shared_ptr<T> &value) noexcept {
            store(value);
        }
        // NOLINTNEXTLINE(*-explicit-constructor)
        SafeSharedPtr &operator=(const std::shared_ptr<T> &value) noexcept {
            store(value);
            return *this;
        }
        // NOLINTNEXTLINE(*-explicit-constructor)
        operator std::shared_ptr<T>() const noexcept {
            return load();
        }
        std::shared_ptr<T> store(const std::shared_ptr<T> &value) {
            std::atomic_store(&_ptr, value);
            return value;
        }
        std::shared_ptr<T> load() const {
            return std::atomic_load(&_ptr);
        }

        [[nodiscard]] bool operator==(const SafeSharedPtr<T> &other) const {
            return load() == other.load();
        }

        [[nodiscard]] bool operator!=(const SafeSharedPtr<T> &other) const {
            return load() != other.load();
        }

        [[nodiscard]] explicit operator bool() const {
            return static_cast<bool>(load());
        }

        [[nodiscard]] bool operator!() const {
            return !load();
        }
    };

} // namespace util
