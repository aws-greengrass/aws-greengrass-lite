#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace util {
    inline bool startsWith(std::string_view target, std::string_view prefix) {
        // prefix that target string starts with prefix string
        if(prefix.length() > target.length()) {
            return false;
        }
        return target.substr(0, prefix.length()) == prefix;
    }

    inline bool endsWith(std::string_view target, std::string_view suffix) {
        // prefix that target string starts with prefix string
        if(suffix.length() > target.length()) {
            return false;
        }
        return target.substr(target.length() - suffix.length(), suffix.length()) == suffix;
    }

    inline std::string_view trimStart(std::string_view target, std::string_view prefix) {
        // remove prefix from start
        if(startsWith(target, prefix)) {
            return target.substr(prefix.length(), target.length() - prefix.length());
        } else {
            return target;
        }
    }

    inline std::string_view trimEnd(std::string_view target, std::string_view suffix) {
        // remove suffix from end
        if(endsWith(target, suffix)) {
            return target.substr(0, target.length() - suffix.length());
        } else {
            return target;
        }
    }

    inline int lowerChar(int c) {
        // important: ignore Locale to ensure portability
        if(c >= 'A' && c <= 'Z') {
            return c - 'A' + 'a';
        } else {
            return c;
        }
    }

    inline std::string lower(std::string_view source) {
        std::string target;
        target.resize(source.size());
        std::transform(source.begin(), source.end(), target.begin(), lowerChar);
        return target;
    }

    //
    // Used for views into memory buffers with safe copies (C++20 has span, C++17 does not)
    //
    template<typename DataT, typename SizeT = std::size_t>
    class Span {
        DataT *_ptr;
        SizeT _len;

    public:
        Span(DataT *ptr, SizeT len) noexcept : _ptr(ptr), _len(len) {
        }

        DataT &operator[](SizeT i) noexcept {
            return *_ptr[i];
        }

        const DataT &operator[](SizeT i) const noexcept {
            return *_ptr[i];
        }

        [[nodiscard]] SizeT size() const noexcept {
            return _len;
        }

        DataT *begin() noexcept {
            return _ptr;
        }

        DataT *end() noexcept {
            return _ptr + _len;
        }

        template<typename OutputIt>
        SizeT copyTo(OutputIt d_first, OutputIt d_last) {
            DataT *s = begin();
            DataT *s_last = end();
            OutputIt d = d_first;
            for(; s != s_last && d != d_last; ++s, ++d) {
                *d = *s;
            }
            return s - begin();
        }

        template<typename InputIt>
        SizeT copyFrom(InputIt s_first, InputIt s_last) {
            DataT *d = begin();
            DataT *d_last = end();
            InputIt s = s_first;
            for(; s != s_last && d != d_last; ++s, ++d) {
                *d = *s;
            }
            return d - begin();
        }
    };

    //
    // Base class for all "by-reference-only" objects
    //
    template<typename T>
    class RefObject : public std::enable_shared_from_this<T> {
    public:
        ~RefObject() = default;
        RefObject();
        RefObject(const RefObject &) = delete;
        RefObject(RefObject &&) noexcept = default;
        RefObject &operator=(const RefObject &) = delete;
        RefObject &operator=(RefObject &&) noexcept = delete;

        std::shared_ptr<const T> baseRef() const {
            return std::enable_shared_from_this<T>::shared_from_this();
        }

        std::shared_ptr<T> baseRef() {
            return std::enable_shared_from_this<T>::shared_from_this();
        }

        template<typename S>
        std::shared_ptr<const S> tryRef() const;
        template<typename S>
        std::shared_ptr<const S> ref() const;
        template<typename S>
        std::shared_ptr<S> tryRef();
        template<typename S>
        std::shared_ptr<S> ref();
    };

    template<typename T>
    RefObject<T>::RefObject() {
        static_assert(std::is_base_of_v<RefObject, T>);
    }

    template<typename T>
    template<typename S>
    std::shared_ptr<const S> RefObject<T>::tryRef() const {
        static_assert(std::is_base_of_v<T, S>);
        if constexpr(std::is_same_v<T, S>) {
            return baseRef();
        } else {
            return std::dynamic_pointer_cast<const S>(baseRef());
        }
    }

    template<typename T>
    template<typename S>
    std::shared_ptr<S> RefObject<T>::tryRef() {
        static_assert(std::is_base_of_v<T, S>);
        if constexpr(std::is_same_v<T, S>) {
            return baseRef();
        } else {
            return std::dynamic_pointer_cast<S>(baseRef());
        }
    }

    template<typename T>
    template<typename S>
    std::shared_ptr<const S> RefObject<T>::ref() const {
        std::shared_ptr<const S> ptr{tryRef<S>()};
        if(!ptr) {
            throw std::bad_cast();
        }
        return ptr;
    }

    template<typename T>
    template<typename S>
    std::shared_ptr<S> RefObject<T>::ref() {
        std::shared_ptr<S> ptr{tryRef<S>()};
        if(!ptr) {
            throw std::bad_cast();
        }
        return ptr;
    }
} // namespace util
