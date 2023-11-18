#pragma once
#include "data/string_table.hpp"
#include <cpp_api.hpp>
#include <optional>
#include <type_traits>

namespace errors {
    class Error;

    /**
     * Utility class to manage thread-local data of current thread error and additional error
     * data, while allowing for memory errors.
     */
    class ThreadErrorContainer {
        // See ThreadContextContainer for local thread issues
        // For performance reasons, non-error is fast tracked
        // Note that destructor cannot access data, use only
        // trivial data values.

        constexpr static int64_t KIND_UNKNOWN{-1};
        constexpr static int64_t KIND_NO_ERROR{0};

        ThreadErrorContainer() = default;
        int64_t _kindSymbolId{KIND_UNKNOWN};

        [[nodiscard]] uint32_t fetchKindAsInt() const;

    public:
        [[nodiscard]] bool hasError() const noexcept {
            return getKindAsInt() != 0;
        }

        [[nodiscard]] uint32_t getKindAsInt() const {
            if(_kindSymbolId >= 0) {
                return static_cast<uint32_t>(_kindSymbolId);
            } else {
                return fetchKindAsInt();
            }
        }

        [[nodiscard]] data::Symbol getCachedKind() const;

        [[nodiscard]] const char *getCachedWhat() const;

        [[nodiscard]] std::optional<Error> getError() const;

        void setError(const Error &error);

        void clear();
        void reset() noexcept;
        void throwIfError();

        /**
         * Retrieve the ThreadErrorContainer singleton.
         */
        static ThreadErrorContainer &get() {
            static thread_local ThreadErrorContainer container;
            return container;
        }
    };

    /**
     * Base class for Nucleus exceptions. This exception class carries through a "kind" symbol
     * that can transition Nucleus/Plugin boundaries.
     */
    class Error : public std::runtime_error {
        data::Symbol _kind;

        template<typename E>
        static data::Symbol typeKind() {
            static_assert(std::is_base_of_v<std::exception, E>);
            return kind(typeid(E).name());
        }

    public:
        Error(const Error &) noexcept = default;
        Error(Error &&) noexcept = default;
        Error &operator=(const Error &) noexcept = default;
        Error &operator=(Error &&) noexcept = default;
        ~Error() override = default;

        explicit Error(data::Symbol kind, const std::string &what = "Unspecified Error") noexcept
            : std::runtime_error(what), _kind(kind) {
        }

        template<typename E>
        static Error of(const E &error) {
            static_assert(std::is_base_of_v<std::exception, E>);
            return Error(typeKind<E>(), error.what());
        }
        [[nodiscard]] constexpr data::Symbol kind() const {
            return _kind;
        }
        static data::Symbol kind(std::string_view kind);

        void toThreadLastError() const {
            ThreadErrorContainer::get().setError(*this);
        }
    };

    template<>
    inline Error Error::of<Error>(const Error &error) {
        return error;
    }

    template<>
    inline Error Error::of<ggapi::GgApiError>(const ggapi::GgApiError &error);

} // namespace errors