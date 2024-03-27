#pragma once

#include "greengrass_traits.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace util {
    template<typename InputIt, typename OutputIt>
    auto bounded_copy(InputIt s_first, InputIt s_last, OutputIt d_first, OutputIt d_last) noexcept {
        if constexpr(is_random_access_iterator<InputIt> && is_random_access_iterator<OutputIt>) {
            return std::copy_n(s_first, std::min(d_last - d_first, s_last - s_first), d_first)
                   - d_first;
        } else {
            std::ptrdiff_t i = 0;
            while(s_first != s_last && d_first != d_last) {
                *d_first++ = *s_first++;
                ++i;
            }
            return i;
        }
    }

    //
    // Used for views into memory buffers with safe copies (C++20 has span, C++17 does not)
    //
    template<typename DataT, typename SizeT = std::size_t>
    class Span {
    public:
        using value_type = DataT;
        using element_type = std::remove_cv_t<value_type>;
        using pointer = value_type *;
        using const_pointer = const value_type *;
        using reference = value_type &;
        using size_type = SizeT;
        using difference_type = std::ptrdiff_t;
        using iterator = pointer;
        using const_iterator = const_pointer;

    private:
        pointer _ptr{nullptr};
        size_type _len{0};

        constexpr void bounds_check(size_type i) const {
            if(i >= size()) {
                throw std::out_of_range("Span idx too big");
            }
            if constexpr(std::is_signed_v<size_type>) {
                if(i < size_type{0}) {
                    throw std::out_of_range("Span negative idx");
                }
            }
        }

    public:
        constexpr Span() noexcept = default;
        constexpr Span(pointer ptr, size_type len) noexcept : _ptr(ptr), _len(len) {
        }

        template<size_t N>
        // NOLINTNEXTLINE (*-c-arrays)
        constexpr Span(type_identity<value_type> (&e)[N]) noexcept
            : _ptr(std::data(e)), _len{std::size(e)} {
        }

        template<size_t N>
        // NOLINTNEXTLINE (*-explicit-constructor)
        constexpr Span(std::array<value_type, N> &arr) noexcept
            : _ptr{std::data(arr)}, _len{std::size(arr)} {
        }

        template<class Traits>
        explicit constexpr Span(std::basic_string_view<element_type, Traits> sv)
            : _ptr{std::data(sv)}, _len{std::size(sv)} {
        }

        template<class Traits, class Alloc>
        explicit constexpr Span(std::basic_string<value_type, Traits, Alloc> &str)
            : _ptr{std::data(str)}, _len{static_cast<SizeT>(std::size(str))} {
        }

        template<typename Alloc>
        explicit constexpr Span(std::vector<value_type, Alloc> &vec)
            : _ptr{std::data(vec)}, _len{std::size(vec)} {
        }

        template<typename Alloc>
        explicit constexpr Span(const std::vector<element_type, Alloc> &vec)
            : _ptr{std::data(vec)}, _len{std::size(vec)} {
        }

        constexpr reference operator[](size_type i) const noexcept {
            return _ptr[i];
        }

        [[nodiscard]] constexpr reference at(size_type i) const {
            bounds_check(i);
            return operator[](i);
        }

        [[nodiscard]] constexpr size_type size() const noexcept {
            return _len;
        }

        [[nodiscard]] constexpr bool empty() const noexcept {
            return size() == size_type{0};
        }

        [[nodiscard]] constexpr size_type size_bytes() const noexcept {
            return size() * sizeof(element_type);
        }

        [[nodiscard]] constexpr pointer data() const noexcept {
            return _ptr;
        }

        [[nodiscard]] constexpr iterator begin() const noexcept {
            return data();
        }

        [[nodiscard]] constexpr iterator end() const noexcept {
            return data() + size();
        }

        [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
            return begin();
        }

        [[nodiscard]] constexpr const_iterator cend() const noexcept {
            return end();
        }

        template<typename OutputIt>
        difference_type copyTo(OutputIt d_first, OutputIt d_last) const noexcept {
            return bounded_copy(cbegin(), cend(), d_first, d_last);
        }

        template<typename InputIt>
        difference_type copyFrom(InputIt s_first, InputIt s_last) const noexcept {
            return bounded_copy(s_first, s_last, begin(), end());
        }

        [[nodiscard]] inline constexpr reference front() const noexcept {
            return *data();
        }

        [[nodiscard]] inline constexpr reference back() const noexcept {
            return *std::prev(end());
        }

        [[nodiscard]] constexpr Span first(size_type n) const noexcept {
            return {data(), n};
        }

        [[nodiscard]] constexpr Span last(size_type n) const noexcept {
            return {end() - n, n};
        }

        [[nodiscard]] constexpr Span subspan(size_type idx, size_type n) const noexcept {
            return {data() + idx, std::min(n, size() - idx)};
        }

        [[nodiscard]] constexpr Span subspan(size_type idx) const noexcept {
            return last(size() - idx);
        }
    };

    // The standard uses std::byte which is clearer, but these are used
    // to send/receive byte arrays from C functions
    template<typename DataT, typename SizeT>
    Span<const char, SizeT> as_bytes(Span<DataT, SizeT> s) {
        // NOLINTNEXTLINE(*-reinterpret-cast)
        return {reinterpret_cast<const char *>(s.data()), s.size_bytes()};
    }

    template<typename DataT, typename SizeT>
    std::enable_if_t<!std::is_const_v<DataT>, Span<char, SizeT>> as_writeable_bytes(
        Span<DataT, SizeT> s) {
        // NOLINTNEXTLINE(*-reinterpret-cast)
        return {reinterpret_cast<char *>(s.data()), s.size_bytes()};
    }

} // namespace util
