#pragma once

#include <c_api.hpp>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace util {

    using namespace std::literals;

    /**
     * This abstraction simplifies Nucleus code
     */
    class RuntimeErrorWithKind : public std::runtime_error {
    public:
        [[nodiscard]] virtual ggapiErrorKind getKindId() const noexcept = 0;
        using std::runtime_error::runtime_error;
    };

    /**
     * Common logic for an error that can pass between Nucleus and Plugins. Such errors are
     * described by the tuple {Kind,Message} where Kind is a non-zero Symbol, and Message is a
     * non-empty string.
     */
    template<typename Traits>
    class ErrorBase : public RuntimeErrorWithKind {
    public:
        using KindType = typename Traits::SymbolType;

    private:
        inline static const auto DEFAULT_ERROR_TEXT = "Unspecified Error"s; // NOLINT(*-err58-cpp)
        inline static const auto DEFAULT_ERROR_KIND =
            Traits::translateKind("ggapi::UnspecifiedError"); // NOLINT(*-err58-cpp)
        inline static const auto RUNTIME_ERROR_KIND =
            Traits::translateKind("std::runtime_error"); // NOLINT(*-err58-cpp)
        inline static const auto LOGICAL_ERROR_KIND =
            Traits::translateKind("std::logic_error"); // NOLINT(*-err58-cpp)
        inline static const auto STD_ERROR_KIND =
            Traits::translateKind("std::exception"); // NOLINT(*-err58-cpp)
        KindType _kind;

    public:
        ErrorBase(const ErrorBase &) noexcept = default;
        ErrorBase(ErrorBase &&) noexcept = default;
        ErrorBase &operator=(const ErrorBase &) noexcept = default;
        ErrorBase &operator=(ErrorBase &&) noexcept = default;
        ~ErrorBase() noexcept override = default;

        explicit ErrorBase(KindType kind, const std::string &what = DEFAULT_ERROR_TEXT) noexcept
            : RuntimeErrorWithKind(what), _kind(kind) {
        }

        explicit ErrorBase(const RuntimeErrorWithKind &other) noexcept
            : ErrorBase(Traits::translateKind(other.getKindId()), other.what()) {
        }

        explicit ErrorBase(
            std::string_view kind, const std::string &what = DEFAULT_ERROR_TEXT) noexcept
            : ErrorBase(Traits::translateKind(kind), what) {
        }

        explicit ErrorBase(const char *kind, const std::string &what = DEFAULT_ERROR_TEXT) noexcept
            : ErrorBase(std::string_view(kind), what) {
        }

        /**
         * Universal symbol ID for Kind
         * @return error kind
         */
        [[nodiscard]] ggapiErrorKind getKindId() const noexcept override {
            return _kind.asInt();
        }

        /**
         * Convert error to Wrapped error preserving some information about the underlying
         * error if possible.
         *
         * @param error Exception pointer
         * @return Wrapped error
         */
        [[nodiscard]] static ErrorBase of(const std::exception_ptr &error) noexcept {
            try {
                if(error) {
                    std::rethrow_exception(error);
                } else {
                    return unspecified();
                }
            } catch(const ErrorBase &err) {
                return err;
            } catch(const RuntimeErrorWithKind &err) {
                return ErrorBase(Traits::translateKind(err.getKindId()), err.what());
            } catch(const std::runtime_error &err) {
                return ErrorBase(RUNTIME_ERROR_KIND, err.what());
            } catch(const std::logic_error &err) {
                return ErrorBase(LOGICAL_ERROR_KIND, err.what());
            } catch(const std::exception &err) {
                return ErrorBase(STD_ERROR_KIND, err.what());
            } catch(...) {
                return unspecified();
            }
        }

        [[nodiscard]] static ErrorBase unspecified() noexcept {
            return ErrorBase(DEFAULT_ERROR_KIND);
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
