#pragma once

#include <c_api.hpp>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace util {

    using namespace std::literals;

    namespace traits {
        //
        // C++17 Template SFINAE to verify given class T has a "KindType" member and a "kind" member
        // function
        //
        template<typename, typename = void>
        struct ClassProvidesKind : std::false_type {};
        template<typename T>
        struct ClassProvidesKind<
            T,
            std::void_t<decltype(std::declval<T>().kind()), typename T::KindType>>
            : std::true_type {};
        template<typename T>
        static constexpr bool classProvidesKind = ClassProvidesKind<T>::value;
    } // namespace traits

    /**
     * Common logic for an error that can pass between Nucleus and Plugins. Such errors are
     * described by the tuple {Kind,Message} where Kind is a non-zero Symbol, and Message is a
     * non-empty string.
     */
    template<typename Traits>
    class ErrorBase : public std::runtime_error {
    public:
        using KindType = typename Traits::SymbolType;

    private:
        inline static const auto DEFAULT_ERROR_TEXT = "Unspecified Error"s; // NOLINT(*-err58-cpp)
        KindType _kind;

        template<typename Error>
        static KindType typeKind() noexcept {
            static_assert(std::is_base_of_v<std::exception, Error>);
            return Traits::translateKind(std::string_view{typeid(Error).name()});
        }

    public:
        ErrorBase(const ErrorBase &) noexcept = default;
        ErrorBase(ErrorBase &&) noexcept = default;
        ErrorBase &operator=(const ErrorBase &) noexcept = default;
        ErrorBase &operator=(ErrorBase &&) noexcept = default;
        ~ErrorBase() noexcept override = default;

        explicit ErrorBase(KindType kind, const std::string &what = DEFAULT_ERROR_TEXT) noexcept
            : std::runtime_error(what), _kind(kind) {
        }

        explicit ErrorBase(
            std::string_view kind, const std::string &what = DEFAULT_ERROR_TEXT) noexcept
            : ErrorBase(Traits::translateKind(kind), what) {
        }

        explicit ErrorBase(const char *kind, const std::string &what = DEFAULT_ERROR_TEXT) noexcept
            : ErrorBase(std::string_view(kind), what) {
        }

        template<typename E>
        [[nodiscard]] static ErrorBase of(const E &error) noexcept {
            static_assert(std::is_base_of_v<std::exception, E>);
            if constexpr(traits::classProvidesKind<E>) {
                return ErrorBase(Traits::translateKind(error.kind()), error.what());
            } else {
                return ErrorBase(typeKind<E>(), error.what());
            }
        }

        [[nodiscard]] static ErrorBase unspecified() noexcept {
            return ErrorBase("unspecified");
        }

        [[nodiscard]] constexpr KindType kind() const noexcept {
            return _kind;
        }

        [[nodiscard]] ggapiErrorKind toThreadLastError() const noexcept {

            return toThreadLastError(_kind, what());
        }

        static ggapiErrorKind toThreadLastError(KindType kind, std::string_view what) noexcept {
            auto errInt = kind.asInt();
            ::ggapiSetError(errInt, what.data(), what.length());
            return errInt;
        }

        static void clearThreadLastError() noexcept {
            ::ggapiSetError(0, nullptr, 0);
        }

        [[nodiscard]] static std::string getThreadErrorMessage() noexcept {
            const char *what = ::ggapiGetErrorWhat();
            if(what) {
                return {what};
            } else {
                return {};
            }
        }

        // TODO: Will be deprecated
        [[nodiscard]] static bool hasThreadLastError() noexcept {
            return ::ggapiGetErrorKind() != 0;
        }

        static void throwThreadError(ggapiErrorKind err) {
            if(err != 0) {
                throwThreadError(Traits::translateKind(err));
            }
        }

        static void throwThreadError(KindType kind) {
            if(kind) {
                std::string msg = getThreadErrorMessage();
                clearThreadLastError();
                throw ErrorBase(kind, msg);
            }
        }

        static void throwIfThreadHasError() {
            throwThreadError(static_cast<ggapiErrorKind>(::ggapiGetErrorKind()));
        }
    };

} // namespace util
