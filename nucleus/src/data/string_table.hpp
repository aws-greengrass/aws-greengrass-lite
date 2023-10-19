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

        struct Hash : protected std::hash<std::string> {
            [[nodiscard]] std::size_t operator()(const InternedString &k) const noexcept {
                return std::hash<std::string>::operator()(k._value);
            }
        };

        InternedString() = default;
        InternedString(const InternedString &) = default;
        InternedString(InternedString &&) = default;
        InternedString &operator=(const InternedString &) = default;
        InternedString &operator=(InternedString &&) = default;
        ~InternedString() = default;

        InternedString(std::string_view sv) : _value{sv} {
        } // NOLINT(*-explicit-constructor)

        InternedString(std::string s) : _value{std::move(s)} {
        } // NOLINT(*-explicit-constructor)

        operator std::string() const { // NOLINT(*-explicit-constructor)
            return _value;
        }
    };

    //
    // Handles for strings only
    //
    typedef Handle<InternedString> StringOrd;

    class StringTable {
        static const int PRIME{15299};
        mutable std::shared_mutex _mutex;
        std::unordered_map<InternedString, StringOrd, InternedString::Hash, InternedString::CompEq>
            _interned;
        std::unordered_map<StringOrd, InternedString, StringOrd::Hash, StringOrd::CompEq> _reverse;

    public:
        StringOrd testAndGetOrd(const std::string &str) const {
            std::shared_lock guard{_mutex};
            auto i = _interned.find(str);
            if(i == _interned.end()) {
                return StringOrd::nullHandle();
            } else {
                return i->second;
            }
        }

        StringOrd getOrCreateOrd(const std::string &str) {
            Handle ord = testAndGetOrd(str); // optimistic using shared lock
            if(ord.isNull()) {
                std::unique_lock guard(_mutex);
                auto i = _interned.find(str);
                if(i == _interned.end()) {
                    // expected case
                    // ordinals are computed this way to optimize std::map
                    StringOrd new_ord{
                        static_cast<uint32_t>(std::hash<std::string>()(str))}; // distribute IDs
                    while(_reverse.find(new_ord) != _reverse.end()) {
                        new_ord = StringOrd{new_ord.asInt() + PRIME};
                    }
                    _reverse[new_ord] = str;
                    _interned[str] = new_ord;
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
        StringOrdInit(const char *_constString) : _string(_constString) {
        }

        // NOLINTNEXTLINE(*-explicit-constructor)
        StringOrdInit(std::string _constString) : _string(_constString) {
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
} // namespace data
