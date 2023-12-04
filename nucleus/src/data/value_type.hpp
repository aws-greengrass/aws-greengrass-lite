#pragma once
#include <variant>

namespace data {
    class Symbol;
    class TrackedObject;

    using ValueTypeBase = std::variant<
        // types in same order as type consts in ValueTypes below
        std::monostate, // Always first (NONE)
        bool, // BOOL
        uint64_t, // INT
        double, // DOUBLE
        std::string, // STRING
        Symbol, // SYMBOL
        std::shared_ptr<TrackedObject> // OBJECT
        >;

    class ValueType : public ValueTypeBase {
    public:
        ValueType() = default;
        ValueType(const ValueType &) = default;
        ValueType(ValueType &&) = default;
        ValueType &operator=(const ValueType &) = default;
        ValueType &operator=(ValueType &&) = default;
        ~ValueType() = default;

        ValueTypeBase &base() {
            return *this;
        }

        [[nodiscard]] const ValueTypeBase &base() const {
            return *this;
        }

        template<typename T>
        // NOLINTNEXTLINE(*-explicit-constructor)
        ValueType(T x) : ValueTypeBase(convert(x)) {
        }

        template<typename T>
        ValueType &operator=(const T &x) {
            ValueTypeBase::operator=(convert(x));
            return *this;
        }

        template<typename T>
        static ValueTypeBase convert(const T &x) {
            if constexpr(std::is_same_v<bool, T>) {
                return ValueTypeBase(x);
            } else if constexpr(std::is_integral_v<T>) {
                return static_cast<uint64_t>(x);
            } else if constexpr(std::is_floating_point_v<T>) {
                return static_cast<double>(x);
            } else if constexpr(std::is_base_of_v<Symbol, T>) {
                return static_cast<Symbol>(x);
            } else if constexpr(std::is_base_of_v<SymbolInit, T>) {
                return x.toSymbol();
            } else if constexpr(std::is_assignable_v<std::shared_ptr<TrackedObject>, T>) {
                return static_cast<std::shared_ptr<TrackedObject>>(x);
            } else if constexpr(std::is_assignable_v<std::string, T>) {
                return static_cast<std::string>(x);
            } else {
                return x;
            }
        }
    };

    // enum class ValueTypes would seem to be better, but we need to compare int to
    // int, so this is easier
    struct ValueTypes {
        static constexpr auto NONE{0};
        static constexpr auto BOOL{1};
        static constexpr auto INT{2};
        static constexpr auto DOUBLE{3};
        static constexpr auto STRING{4};
        static constexpr auto SYMBOL{5};
        static constexpr auto OBJECT{6};
    };

} // namespace data
