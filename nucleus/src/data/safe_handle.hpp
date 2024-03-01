#pragma once
#include "scope/fixed_pointer.hpp"
#include <memory>

namespace scope {
    class Context;
}

namespace data {
    class ObjectAnchor;

    /**
     * Templated class for partial handles (no table reference)
     */
    class PartialHandle {
    private:
        uint32_t _asInt;

    public:
        struct CompEq {
            [[nodiscard]] bool operator()(
                const PartialHandle a, const PartialHandle b) const noexcept {
                return a._asInt == b._asInt;
            }
        };

        struct CompLess {
            [[nodiscard]] bool operator()(
                const PartialHandle a, const PartialHandle b) const noexcept {
                return a._asInt < b._asInt;
            }
        };

        struct Hash {
            [[nodiscard]] std::size_t operator()(const PartialHandle k) const noexcept {
                return k._asInt;
            }
        };

        constexpr PartialHandle() noexcept : _asInt{0} {
        }

        explicit constexpr PartialHandle(const uint32_t i) noexcept : _asInt{i} {
        }

        constexpr PartialHandle(const PartialHandle &) noexcept = default;
        constexpr PartialHandle(PartialHandle &&) noexcept = default;
        ~PartialHandle() noexcept = default;

        constexpr PartialHandle &operator=(const PartialHandle &) noexcept = default;
        constexpr PartialHandle &operator=(PartialHandle &&) noexcept = default;

        [[nodiscard]] uint32_t asInt() const noexcept {
            return _asInt;
        }

        [[nodiscard]] bool isNull() const noexcept {
            return _asInt == 0;
        }

        explicit operator bool() const noexcept {
            return !isNull();
        }

        bool operator!() const noexcept {
            return isNull();
        }

        bool operator==(PartialHandle other) const noexcept {
            return _asInt == other._asInt;
        }

        bool operator!=(PartialHandle other) const noexcept {
            return _asInt != other._asInt;
        }
    };

    /**
     * Templated class for full (resolvable) handles
     */
    template<typename TableType>
    class Handle {
    public:
        using Partial = PartialHandle;

    private:
        Partial _partial;
        mutable scope::FixedPtr<TableType> _table;

    public:
        constexpr Handle() noexcept = default;
        constexpr Handle(const Handle &) noexcept = default;
        constexpr Handle(Handle &&) noexcept = default;
        constexpr Handle &operator=(const Handle &) noexcept = default;
        constexpr Handle &operator=(Handle &&) noexcept = default;
        ~Handle() noexcept = default;

        constexpr Handle(scope::FixedPtr<TableType> table, Partial h) noexcept
            : _partial(h), _table(table) {
        }

        [[nodiscard]] uint32_t asInt() const noexcept {
            return _partial.asInt();
        }

        [[nodiscard]] TableType &table() noexcept {
            return *_table;
        }

        [[nodiscard]] const TableType &table() const noexcept {
            return *_table;
        }

        [[nodiscard]] Partial partial() const noexcept {
            return _partial;
        }

        [[nodiscard]] bool isNull() const noexcept {
            return _partial.isNull();
        }

        [[nodiscard]] Partial detach() noexcept {
            auto p = _partial;
            _partial = {};
            return p;
        }

        explicit operator bool() const noexcept {
            return !isNull();
        }

        bool operator!() const noexcept {
            return isNull();
        }

        bool operator==(Handle other) const noexcept {
            return _partial == other._partial && _table == other._table;
        }

        bool operator!=(Handle other) const noexcept {
            return _partial != other._partial || _table != other._table;
        }
    };

} // namespace data
