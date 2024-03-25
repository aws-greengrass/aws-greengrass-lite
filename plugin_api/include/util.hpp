#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <type_traits>
namespace util {
    template<typename D, typename S>
    inline D safeBound(const S value, const D min, const D max) {
        return static_cast<D>(std::max(std::min(value, static_cast<S>(max)), static_cast<S>(min)));
    }

    template<typename D, typename S>
    inline D safeBound(const S value) {
        return safeBound<D, S>(value, std::numeric_limits<D>::min(), std::numeric_limits<D>::max());
    }

    template<typename D, typename S>
    inline D safeBoundPositive(const S value) {
        return safeBound<D, S>(value, 0, std::numeric_limits<D>::max());
    }

    /**
     * A class based Enum bound to a set of valid simple type values.
     * @tparam EType Base Enum Type
     * @tparam EVals Set of valid Enum values
     */
    template<typename EType, EType... EVals>
    class Enum {
    public:
        using BaseType = EType;
        static constexpr std::array values{EVals...};
        static constexpr uint32_t count = values.size();

        template<size_t index>
        static constexpr BaseType get() noexcept {
            return std::get<index>(values);
        }

        template<EType EVal>
        using ConstType = std::integral_constant<EType, EVal>;

        template<typename Ret, typename Func>
        static std::optional<Ret> visit(const BaseType &v, Func &&func) {
            std::optional<Ret> r{};
            // Invoke the visitor only for the matching enum value (logical-&& short-circuiting).
            // Stop searching once the visitor is called (logical-|| short-circuiting).
            (((v == EVals)
              && ((r = std::invoke(std::forward<Func>(func), ConstType<EVals>{})), true))
             || ...);
            return r;
        }

        template<typename Func>
        static void visitNoRet(const BaseType &v, Func &&func) {
            (((v == EVals) && ((std::invoke(std::forward<Func>(func), ConstType<EVals>{})), true))
             || ...);
        }
    };

} // namespace util
