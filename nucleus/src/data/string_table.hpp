#pragma once
#include "safe_handle.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace data {

    class Environment;

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
    using StringOrd = Handle<InternedString>;

    class StringTable {
        static const int PRIME{15299};
        mutable std::shared_mutex _mutex;
        std::unordered_map<InternedString, StringOrd, InternedString::Hash, InternedString::CompEq>
            _interned;
        std::unordered_map<StringOrd, InternedString, StringOrd::Hash, StringOrd::CompEq> _reverse;

    public:
        StringOrd testAndGetOrd(std::string_view str) const {
            std::shared_lock guard{_mutex};
            auto i = _interned.find(str);
            if(i == _interned.end()) {
                return StringOrd::nullHandle();
            } else {
                return i->second;
            }
        }

        StringOrd getOrCreateOrd(std::string_view str) {
            Handle ord = testAndGetOrd(str); // optimistic using shared lock
            if(ord.isNull()) {
                std::unique_lock guard(_mutex);
                auto i = _interned.find(str);
                if(i == _interned.end()) {
                    // expected case
                    // ordinals are computed this way to optimize std::map
                    StringOrd new_ord{
                        static_cast<uint32_t>(InternedString::Hash{}(str))}; // distribute IDs
                    while(_reverse.find(new_ord) != _reverse.end()) {
                        new_ord = StringOrd{new_ord.asInt() + PRIME};
                    }
                    _reverse.emplace(new_ord, str);
                    _interned.emplace(str, new_ord);
                    return new_ord;
                } else {
                    // this path handles a race condition
                    return i->second;
                }
            } else {
                return ord;
            }
        }

        bool isStringOrdValid(StringOrd ord) const {
            std::shared_lock guard{_mutex};
            return _reverse.find(ord) != _reverse.end();
        }

        std::string getString(StringOrd ord) const {
            std::shared_lock guard{_mutex};
            return _reverse.at(ord);
        }

        template<typename StringLike>
        std::string getStringOr(StringOrd ord, StringLike &&default_value) const {
            static_assert(
                std::is_convertible_v<StringLike, std::string>, "Requires stringlike type"
            );
            if(!ord) {
                return {std::forward<StringLike>(default_value)};
            }
            return getString(ord);
        }

        void assertStringHandle(const StringOrd ord) const {
            if(!isStringOrdValid(ord)) {
                throw std::invalid_argument("String ordinal is not valid");
            }
        }
    };

    //
    // Helper class for dealing with large numbers of constants
    //
    class StringOrdInit {
        mutable StringOrd _ord{};
        std::string _string;
        void init(Environment &environment) const;

    public:
        // NOLINTNEXTLINE(*-explicit-constructor)
        StringOrdInit(const char *constString) : _string(constString) {
        }

        // NOLINTNEXTLINE(*-explicit-constructor)
        StringOrdInit(std::string_view constString) : _string(constString) {
        }

        // NOLINTNEXTLINE(*-explicit-constructor)
        operator StringOrd() const {
            assert(!_ord.isNull());
            return _ord;
        }

        // NOLINTNEXTLINE(*-explicit-constructor)
        operator std::string() const {
            return _string;
        }

        StringOrdInit operator+(const StringOrdInit &other) const {
            StringOrdInit result{_string};
            result._string += other._string;
            return result;
        }

        static void init(Environment &environment, std::initializer_list<StringOrdInit> list);
    };

    //
    // Helper class for when we need to pass string table reference with string ordinal
    //
    class StringOrdExt {
        // Code assumes global lifetime safety
        StringTable *_table;
        // Doing as base-class causes slicing warnings
        StringOrd _ord;

    public:
        StringOrdExt(const StringOrdExt &) = default;
        StringOrdExt(StringOrdExt &&) = default;
        StringOrdExt &operator=(const StringOrdExt &) = default;
        StringOrdExt &operator=(StringOrdExt &&) = default;
        ~StringOrdExt() = default;

        bool operator==(const StringOrdExt &other) const {
            return this == &other || (this->_ord == other._ord && this->_table == other._table);
        }

        bool operator!=(const StringOrdExt &other) const {
            return !(*this == other);
        }

        StringOrdExt(StringTable &table, StringOrd ord) : _table(&table), _ord(ord) {
        }

        StringOrdExt(Environment &env, StringOrd ord);
        StringOrdExt(StringTable &table, std::string_view str);
        StringOrdExt(Environment &env, std::string_view str);

        [[nodiscard]] std::string asString() const {
            if(_ord.isNull()) {
                return "";
            } else {
                return _table->getString(_ord);
            }
        }

        [[nodiscard]] StringOrd asStringOrd() const {
            return _ord;
        }

        [[nodiscard]] uint32_t asInt() const {
            return _ord.asInt();
        }

        [[nodiscard]] bool isNull() const {
            return _ord.isNull();
        }

        // NOLINTNEXTLINE(*-explicit-constructor)
        operator std::string() const {
            return asString();
        }

        // NOLINTNEXTLINE(*-explicit-constructor)
        operator StringOrd() const {
            return asStringOrd();
        }
    };

} // namespace data
