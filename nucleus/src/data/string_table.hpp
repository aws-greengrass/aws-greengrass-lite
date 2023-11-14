#pragma once
#include "data/safe_handle.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace scope {
    class Context;
}

namespace data {
    class ValueType;

    //
    // Internalized string (right now simple a wrapper around string)
    //
    class InternedString {
        std::string _value;

    public:
        struct CompEq {
            [[nodiscard]] bool operator()(const InternedString &a, const InternedString &b)
                const noexcept {
                return a._value == b._value;
            }
        };

        struct CompLess {
            [[nodiscard]] bool operator()(const InternedString &a, const InternedString &b)
                const noexcept {
                return a._value < b._value;
            }
        };

        struct Hash {
            using hash_type = std::hash<std::string_view>;
            using is_transparent = void;

            [[nodiscard]] std::size_t operator()(const InternedString &k) const noexcept {
                return hash_type{}(k._value);
            }

            [[nodiscard]] std::size_t operator()(const std::string &s) const noexcept {
                return hash_type{}(s);
            }

            [[nodiscard]] std::size_t operator()(std::string_view sv) const noexcept {
                return hash_type{}(sv);
            }
        };

        InternedString() = default;
        InternedString(const InternedString &) = default;
        InternedString(InternedString &&) = default;
        InternedString &operator=(const InternedString &) = default;
        InternedString &operator=(InternedString &&) = default;
        ~InternedString() = default;

        // NOLINTNEXTLINE(*-explicit-constructor)
        InternedString(std::string_view sv) : _value{sv} {
        }

        // NOLINTNEXTLINE(*-explicit-constructor)
        InternedString(std::string s) : _value{std::move(s)} {
        }

        // NOLINTNEXTLINE(*-explicit-constructor)
        [[nodiscard]] operator std::string() const {
            return _value;
        }
    };

    //
    // Handles for strings only
    //
    class SymbolTable;

    class Symbol : public Handle<SymbolTable> {
    public:
        constexpr Symbol() noexcept = default;
        constexpr Symbol(const Symbol &) noexcept = default;
        constexpr Symbol(Symbol &&) noexcept = default;
        constexpr Symbol &operator=(const Symbol &) noexcept = default;
        constexpr Symbol &operator=(Symbol &&) noexcept = default;
        ~Symbol() noexcept = default;

        constexpr Symbol(scope::FixedPtr<SymbolTable> table, Partial h) noexcept
            : Handle(table, h) {
        }

        std::string toString() const;

        // NOLINTNEXTLINE(*-explicit-constructor)
        operator std::string() const {
            return toString();
        }

        template<typename StringLike>
        std::string toStringOr(StringLike &&default_value) const {
            static_assert(
                std::is_convertible_v<StringLike, std::string>, "Requires stringlike type");
            if(isNull()) {
                return {std::forward<StringLike>(default_value)};
            } else {
                return toString();
            }
        }
    };

    class SymbolTable {
        static const int PRIME{15299};
        mutable std::shared_mutex _mutex;
        mutable scope::FixedPtr<SymbolTable> _table{scope::FixedPtr<SymbolTable>::of(this)};
        std::unordered_map<
            InternedString,
            Symbol::Partial,
            InternedString::Hash,
            InternedString::CompEq>
            _interned;
        std::unordered_map<
            Symbol::Partial,
            InternedString,
            Symbol::Partial::Hash,
            Symbol::Partial::CompEq>
            _reverse;

        Symbol applyUnchecked(Symbol::Partial h) const {
            return {_table, h};
        }

    public:
        Symbol testAndGetSymbol(std::string_view str) const {
            std::shared_lock guard{_mutex};
            auto i = _interned.find(str);
            if(i == _interned.end()) {
                return {};
            } else {
                return applyUnchecked(i->second);
            }
        }

        Symbol intern(std::string_view str) {
            Symbol ord = testAndGetSymbol(str); // optimistic using shared lock
            if(ord.isNull()) {
                std::unique_lock guard(_mutex);
                auto i = _interned.find(str);
                if(i == _interned.end()) {
                    // expected case
                    // ordinals are computed this way to optimize std::map
                    Symbol::Partial new_ord{
                        static_cast<uint32_t>(InternedString::Hash{}(str))}; // distribute IDs
                    while(_reverse.find(new_ord) != _reverse.end()) {
                        new_ord = Symbol::Partial{new_ord.asInt() + PRIME};
                    }
                    _reverse.emplace(new_ord, str);
                    _interned.emplace(str, new_ord);
                    return applyUnchecked(new_ord);
                } else {
                    // this path handles a race condition
                    return applyUnchecked(i->second);
                }
            } else {
                return ord;
            }
        }

        bool isSymbolValid(Symbol::Partial symbol) const {
            std::shared_lock guard{_mutex};
            return _reverse.find(symbol) != _reverse.end();
        }

        bool isSymbolValid(const Symbol &symbol) const {
            return isSymbolValid(symbol.partial());
        };

        std::string getString(Symbol::Partial symbol) const {
            std::shared_lock guard{_mutex};
            return _reverse.at(symbol);
        }

        void assertValidSymbol(const Symbol::Partial symbol) const {
            if(!isSymbolValid(symbol)) {
                throw std::invalid_argument("String ordinal is not valid");
            }
        }

        // NOLINTNEXTLINE(readability-convert-member-functions-to-static) false due to assert
        Symbol::Partial partial(const Symbol &symbol) const {
            if(!symbol) {
                return {};
            }
            assert(this == &symbol.table());
            return symbol.partial();
        }

        Symbol apply(const Symbol::Partial symbol) const {
            if(!symbol) {
                return {};
            }
            assertValidSymbol(symbol);
            return {_table, symbol};
        }
    };

    inline std::string Symbol::toString() const {
        return table().getString(partial());
    }

    //
    // Helper class for dealing with large numbers of constants
    //
    class SymbolInit {
        mutable Symbol _symbol{};
        std::string _string;
        void init(const std::shared_ptr<scope::Context> &context) const;

    public:
        // NOLINTNEXTLINE(*-explicit-constructor)
        SymbolInit(const char *constString) : _string(constString) {
        }

        // NOLINTNEXTLINE(*-explicit-constructor)
        SymbolInit(std::string_view constString) : _string(constString) {
        }

        std::string toString() const {
            return _string;
        }

        Symbol toSymbol() const {
            assert(!_symbol.isNull());
            return _symbol;
        }

        // NOLINTNEXTLINE(*-explicit-constructor)
        operator Symbol() const {
            return toSymbol();
        }

        // NOLINTNEXTLINE(*-explicit-constructor)
        operator std::string() const {
            return toString();
        }

        static void init(
            const std::shared_ptr<scope::Context> &context,
            std::initializer_list<const SymbolInit *> list);
    };

    inline std::string operator+(const Symbol &x, const std::string &y) {
        return x.toString() + y;
    }

    inline std::string operator+(const std::string &x, const Symbol &y) {
        return x + y.toString();
    }

    inline std::string operator+(const Symbol &x, const Symbol &y) {
        return x.toString() + y.toString();
    }

    inline std::string operator+(const SymbolInit &x, const std::string &y) {
        return x.toString() + y;
    }

    inline std::string operator+(const std::string &x, const SymbolInit &y) {
        return x + y.toString();
    }

    inline std::string operator+(const SymbolInit &x, const SymbolInit &y) {
        return x.toString() + y.toString();
    }

} // namespace data
