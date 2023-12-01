#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

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

    template<class It>
    static constexpr bool is_random_access_iterator = std::is_base_of_v<
        std::random_access_iterator_tag,
        typename std::iterator_traits<It>::iterator_category>;

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

    template<class T>
    using type_identity = T;

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
        explicit constexpr Span(std::basic_string_view<std::remove_const_t<value_type>, Traits> sv)
            : _ptr{std::data(sv)}, _len{std::size(sv)} {
        }

        template<class Traits, class Alloc>
        explicit constexpr Span(std::basic_string<value_type, Traits, Alloc> &str)
            : _ptr{str.data()}, _len{std::size(str)} {
        }

        template<typename Alloc>
        explicit constexpr Span(std::vector<value_type, Alloc> &vec)
            : _ptr{std::data(vec)}, _len{std::size(vec)} {
        }

        constexpr reference operator[](size_type i) const noexcept {
            return _ptr[i];
        }

        constexpr reference at(size_type i) const {
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

        constexpr pointer data() const noexcept {
            return _ptr;
        }

        constexpr iterator begin() const noexcept {
            return data();
        }

        constexpr iterator end() const noexcept {
            return data() + size();
        }

        constexpr const_iterator cbegin() const noexcept {
            return begin();
        }

        constexpr const_iterator cend() const noexcept {
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

        constexpr Span first(size_type n) const noexcept {
            return {data(), n};
        }

        constexpr Span last(size_type n) const noexcept {
            return {end() - n, n};
        }

        constexpr Span subspan(size_type idx, size_type n) const noexcept {
            return {data() + idx, std::min(n, size() - idx)};
        }

        constexpr Span subspan(size_type idx) const noexcept {
            return last(size() - idx);
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

    /**
     * Create a simple 1:1 lookup table (e.g. Symbols to Enums)
     */
    template<typename VT1, typename VT2, uint32_t Nm>
    class LookupTable {
        std::array<VT1, Nm> _first;
        std::array<VT2, Nm> _second;
        template<uint32_t index, typename... Rest>
        void place() noexcept {
            static_assert(index == Nm);
        }
        template<uint32_t index, typename V1, typename V2, typename... Rest>
        void place(const V1 &v1, const V2 &v2, const Rest &...rest) noexcept {
            _first[index] = std::move(v1);
            _second[index] = std::move(v2);
            place<index + 1, Rest...>(rest...);
        }

    public:
        template<typename... Ts>
        explicit LookupTable(const Ts &...args) noexcept {
            place<0, Ts...>(args...);
        }

        [[nodiscard]] std::optional<VT2> lookup(const VT1 &v) const noexcept {
            auto i = indexOf(v);
            if(i.has_value()) {
                return _second[i.value()];
            } else {
                return {};
            }
        }

        [[nodiscard]] std::optional<VT1> rlookup(const VT2 &v) const noexcept {
            auto i = rindexOf(v);
            if(i.has_value()) {
                return _first[i.value()];
            } else {
                return {};
            }
        }

        [[nodiscard]] std::optional<uint32_t> indexOf(const VT1 &v) const noexcept {
            for(uint32_t i = 0; i < Nm; ++i) {
                if(_first[i] == v) {
                    return i;
                }
            }
            return {};
        }

        [[nodiscard]] std::optional<uint32_t> rindexOf(const VT2 &v) const noexcept {
            for(uint32_t i = 0; i < Nm; ++i) {
                if(_second[i] == v) {
                    return i;
                }
            }
            return {};
        }
    };
    template<typename VT1, typename VT2, typename... Rest>
    LookupTable(const VT1 &v1, const VT2 &v2, const Rest &...args)
        -> LookupTable<VT1, VT2, 1 + sizeof...(Rest) / 2>;

    /**
     * Given a template of index, type, and values, it will retrieve the nth value
     * from the template list of values. Essentially a template const lookup.
     *
     * TmplConstAt<2,int, -1,100,200,300,400>::value == 200
     *
     * @tparam n Index of value to obtain, > 0
     * @tparam T Type of value
     * @tparam Vnext Next value (discarded)
     * @tparam Vrest Rest of the values
     */
    template<uint32_t n, typename T, T Vnext, T... Vrest>
    struct TmplConstAt {
        static constexpr T value = TmplConstAt<n - 1, T, Vrest...>::value;
    };
    /**
     * Given a template of index, type, and values, it will retrieve the nth value
     * from the template list of values. Essentially a template const lookup.
     *
     * Specialized for n=0
     *
     * @tparam T Type of value
     * @tparam Vnext Desired value (used)
     * @tparam Vrest Rest of the values (discarded)
     */
    template<typename T, T Vnext, T... Vrest>
    struct TmplConstAt<0, T, Vnext, Vrest...> {
        static constexpr T value = Vnext;
    };

    /**
     * Given a template of index, and list of types, it will retrieve the nth type
     * from the list of types. Essentially a template type lookup
     *
     * TmplTypeAt<2,Z,A,B,C>::type == B
     *
     * @tparam n Index of value to obtain, > 0
     * @tparam Tnext Next type (discarded)
     * @tparam Trest Rest of the types
     */
    template<uint32_t n, typename Tnext, typename... Trest>
    struct TmplTypeAt {
        using type = typename TmplTypeAt<n - 1, Tnext, Trest...>::type;
    };
    /**
     * Given a template of index, type, and values, it will retrieve the nth value
     * from the template list of values. Essentially a template const lookup.
     *
     * Specialized for n=0
     *
     * @tparam Tnext Desired value (used)
     * @tparam Trest Rest of the values (discarded)
     */
    template<typename Tnext, typename... Trest>
    struct TmplTypeAt<0, Tnext, Trest...> {
        using type = Tnext;
    };

    //
    // Enum magic to allow a unique type per enum value and visitor patterns around it
    //

    /**
     * An Enum constant that is a unique class for each constant value. It is legal to create
     * instances of this class, which simplifies use as constexpr.
     *
     * @tparam EType Base Enum Type, typically "enum class" but can be any simple type.
     * @tparam enumVal simple type value - must be of type EType.
     */
    template<typename EType, EType enumVal>
    class EnumConst {

    public:
        static constexpr EType v = enumVal;

        constexpr EnumConst() noexcept = default;
    };

    /**
     * A class based Enum bound to a set of valid simple type values.
     * @tparam EType Base Enum Type
     * @tparam EVals Set of valid Enum values
     */
    template<typename EType, EType... EVals>
    class Enum {

        template<typename Ret, typename Func, EType Vfirst, EType... Vrest>
        constexpr static std::optional<Ret> _visit(const EType &v, Func &&func) {
            if(v == Vfirst) {
                return std::invoke(std::forward<Func>(func), enumConst<Vfirst>);
            } else {
                return _visit<Ret, Func, Vrest...>(v, std::forward<Func>(func));
            }
        }

        template<typename Ret, typename Func>
        constexpr static std::optional<Ret> _visit(const EType &, Func &&) {
            return {};
        }

    public:
        using BaseType = EType;
        static constexpr uint32_t count = sizeof...(EVals);
        template<EType T>
        using ConstType = EnumConst<EType, T>;
        template<EType T>
        static constexpr ConstType<T> enumConst = ConstType<T>();

        template<uint32_t index>
        static constexpr auto enumAt = TmplConstAt<index, EType, EVals...>::value;
        template<uint32_t index>
        using ConstTypeAt = ConstType<enumAt<index>>;
        template<uint32_t index>
        static constexpr ConstTypeAt<index> enumConstAt = enumConst<enumAt<index>>;

        template<typename Ret, typename Func>
        static std::optional<Ret> visit(const BaseType &v, Func &&func) {
            return _visit<Ret, Func, EVals...>(v, std::forward<Func>(func));
        }
    };

} // namespace util
