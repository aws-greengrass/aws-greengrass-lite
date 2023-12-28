#pragma once
#include "data/data_util.hpp"
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
#include <vector>

namespace scope {
    class Context;
}

namespace data {
    class ValueType;

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
        class StringSpan {
            uint32_t _offset{0};
            uint32_t _len{0};

        public:
            constexpr StringSpan() noexcept = default;
            constexpr StringSpan(uint32_t offset, uint32_t len) noexcept
                : _offset(offset), _len(len) {
            }
            [[nodiscard]] uint32_t offset() const noexcept {
                return _offset;
            }
            [[nodiscard]] uint32_t len() const noexcept {
                return _len;
            }
        };
        class Buffer {
            std::vector<char> _strings;
            std::vector<StringSpan> _spans;
            constexpr static auto CHAR_CAPACITY_SPARE{0x3000};
            constexpr static auto SPAN_CAPACITY_SPARE{0x800};

            static uint32_t indexOf(Symbol::Partial h) {
                return data::IdObfuscator::deobfuscate(h.asInt());
            }

            static Symbol::Partial symbolOf(uint32_t idx) {
                return Symbol::Partial{data::IdObfuscator::obfuscate(idx)};
            }

            constexpr static auto EMPTY_LENGTH_INDEX = 0;

        public:
            Buffer();
            Symbol::Partial push(std::string_view &source);
            static Symbol::Partial empty();

            [[nodiscard]] std::string_view toView(const StringSpan &span) const {
                return {&_strings.at(span.offset()), span.len()};
            }

            [[nodiscard]] uint32_t size() const {
                return _spans.size();
            }

            [[nodiscard]] bool isValid(Symbol::Partial symbol) const {
                return indexOf(symbol) < size();
            }

            [[nodiscard]] StringSpan getSpan(Symbol::Partial symbol) const;

            [[nodiscard]] std::string_view at(Symbol::Partial symbol) const {
                return toView(getSpan(symbol));
            }
        };
        class RbCompLess {
            Buffer &_buffer;

        public:
            using is_transparent = void;

            explicit RbCompLess(Buffer &b) : _buffer(b) {
            }
            [[nodiscard]] bool operator()(const StringSpan &a, const StringSpan &b) const noexcept {
                return _buffer.toView(a) < _buffer.toView(b);
            }
            [[nodiscard]] bool operator()(const StringSpan &a, std::string_view b) const noexcept {
                return _buffer.toView(a) < b;
            }
            [[nodiscard]] bool operator()(std::string_view a, const StringSpan &b) const noexcept {
                return a < _buffer.toView(b);
            }
        };

        mutable std::shared_mutex _mutex;
        mutable scope::FixedPtr<SymbolTable> _table{scope::FixedPtr<SymbolTable>::of(this)};
        Buffer _buffer;
        RbCompLess _rbCompLess{_buffer};
        std::map<StringSpan, Symbol::Partial, RbCompLess> _lookup{_rbCompLess};

        Symbol applyUnchecked(Symbol::Partial h) const {
            return {_table, h};
        }

    public:
        SymbolTable();
        Symbol testAndGetSymbol(std::string_view str) const {
            std::shared_lock guard{_mutex};
            auto i = _lookup.find(str);
            if(i == _lookup.end()) {
                return {};
            } else {
                return applyUnchecked(i->second);
            }
        }

        Symbol intern(std::string_view str);

        bool isSymbolValid(Symbol::Partial symbol) const {
            std::shared_lock guard{_mutex};
            return _buffer.isValid(symbol);
        }

        bool isSymbolValid(const Symbol &symbol) const {
            return isSymbolValid(symbol.partial());
        };

        std::string getString(Symbol::Partial symbol) const {
            std::shared_lock guard{_mutex};
            return std::string(_buffer.at(symbol));
        }

        void assertValidSymbol(Symbol::Partial symbol) const;

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

    /**
     * Helper class for dealing with symbol constants
     * TODO: Change all uses of SymbolInit to inline static const
     */
    class SymbolInit {
        std::string_view _string;
        mutable Symbol _symbol{};
        mutable std::once_flag _onceFlag;
        void init(const std::shared_ptr<scope::Context> &context) const;
        void initOnce(const std::shared_ptr<scope::Context> &context) const;
        void initOnce() const;

    public:
        // SymbolInit is never intended to be used as a parameter for move/copy - it's purpose
        // is to help define symbol constants.
        SymbolInit(const SymbolInit &) = delete;
        SymbolInit(SymbolInit &&) = delete;
        SymbolInit &operator=(const SymbolInit &) = delete;
        SymbolInit &operator=(SymbolInit &&) = delete;
        ~SymbolInit() = default;

        // NOLINTNEXTLINE(*-explicit-constructor)
        SymbolInit(std::string_view constString) noexcept : _string(constString) {
        }

        std::string toString() const {
            return std::string(_string);
        }

        Symbol toSymbol() const {
            initOnce();
            return _symbol;
        }

        // NOLINTNEXTLINE(*-explicit-constructor)
        operator ValueType() const;

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

    /**
     * Helper for use-cases where symbol is required, but string can
     * be provided instead
     */
    class Symbolish : public Symbol {

    public:
        // NOLINTNEXTLINE(*-explicit-constructor)
        Symbolish(std::string_view constString);
        // NOLINTNEXTLINE(*-explicit-constructor)
        Symbolish(const char *constString);
        // NOLINTNEXTLINE(*-explicit-constructor)
        constexpr Symbolish(const Symbol &symbol) noexcept : Symbol(symbol) {
        }
    };

} // namespace data
