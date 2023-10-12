#pragma once
#include <memory>

namespace data {
    class ObjectAnchor;

    //
    // Templated class for handles
    //
    template<typename T>
    class Handle {
    private:
        uint32_t _asInt;

    public:
        struct CompEq {
            [[nodiscard]] bool operator()(const Handle a, const Handle b) const noexcept {
                return a._asInt == b._asInt;
            }
        };

        struct CompLess {
            [[nodiscard]] bool operator()(const Handle a, const Handle b) const noexcept {
                return a._asInt < b._asInt;
            }
        };

        struct Hash {
            [[nodiscard]] std::size_t operator()(const Handle k) const noexcept {
                return k._asInt;
            }
        };

        constexpr Handle() noexcept : _asInt{0} {
        }

        explicit constexpr Handle(const uint32_t i) noexcept : _asInt{i} {
        }

        constexpr Handle(const Handle &) noexcept = default;
        constexpr Handle(Handle &&) noexcept = default;
        ~Handle() noexcept = default;

        constexpr Handle &operator=(const Handle &) noexcept = default;
        constexpr Handle &operator=(Handle &&) noexcept = default;

        [[nodiscard]] uint32_t asInt() const noexcept {
            return _asInt;
        }

        [[nodiscard]] bool isNull() const noexcept {
            return _asInt == 0;
        }

        explicit operator bool() const noexcept {
            return _asInt != 0;
        }

        bool operator!() const noexcept {
            return _asInt == 0;
        }

        bool operator==(Handle other) const noexcept {
            return _asInt == other._asInt;
        }

        bool operator!=(Handle other) const noexcept {
            return _asInt != other._asInt;
        }

        static constexpr Handle nullHandle() noexcept;
    };

    template<typename T>
    inline constexpr Handle<T> Handle<T>::nullHandle() noexcept {
        return {};
    }
} // namespace data
