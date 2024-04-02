#pragma once
#include <iterator>
#include <type_traits>
namespace util {
    template<typename Test, template<typename...> class Ref>
    struct is_specialization : std::false_type {};

    template<template<typename...> class Ref, typename... Args>
    struct is_specialization<Ref<Args...>, Ref> : std::true_type {};

    template<class It>
    static constexpr bool is_random_access_iterator = std::is_base_of_v<
        std::random_access_iterator_tag,
        typename std::iterator_traits<It>::iterator_category>;

    // Allows some class template argument deduction guides to work
    template<class T>
    using type_identity = T;

    // traits::always_false
    namespace traits {
        namespace detail {
            struct _reservedType {};
        } // namespace detail

        template<class... T>
        struct always_false : std::false_type {};

        // a static_assert which always fails is ill-formed in C++17
        // in C++23, static_assert(false) in an instantiated context is OK
        // Supports construct static_assert(traits::always_false_v);
        template<>
        struct always_false<detail::_reservedType> : std::true_type {};

        template<class... T>
        static constexpr bool always_false_v = always_false<T...>::value;
    } // namespace traits

    // traits::isOptional<std::optional<XYZ>> = true
    namespace traits {
        template<typename T>
        using OptionalBaseType = std::invoke_result<decltype(std::declval<T>().value())>;
        template<typename, typename = void>
        struct IsOptional : std::false_type {};
        template<typename T>
        struct IsOptional<
            T,
            std::void_t<
                decltype(std::declval<T>().has_value()),
                OptionalBaseType<T>,
                std::enable_if_t<std::is_default_constructible_v<T>>>> : std::true_type {};
        template<typename T>
        static constexpr bool isOptional = IsOptional<T>::value;
    } // namespace traits

    // traits::isListLike<std::vector<XYZ>> = true
    namespace traits {
        template<typename, typename = void>
        struct IsListLike : std::false_type {};
        template<typename T>
        struct IsListLike<
            T,
            std::void_t<
                decltype(std::declval<T>().begin()),
                decltype(std::declval<T>().end()),
                decltype(std::declval<T>().clear()),
                decltype(std::declval<T>().pop_back()),
                typename T::value_type>> : std::true_type {};
        template<typename T>
        static constexpr bool isListLike = IsListLike<T>::value;
    } // namespace traits

} // namespace util
