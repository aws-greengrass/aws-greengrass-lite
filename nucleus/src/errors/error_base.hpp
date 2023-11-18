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

        ThreadErrorContainer() = default;
        uint32_t _kindSymbolId{0};

    public:
        bool hasError() const noexcept {
            return _kindSymbolId != 0;
        }

        uint32_t getKindAsInt() const noexcept {
            return _kindSymbolId;
        }

        data::Symbol::Partial getKindPartial() const noexcept;

        data::Symbol getCachedKind() const;

        const char *getCachedWhat() const;

        std::optional<Error> getError() const;

        void setError(const Error &error);

        void clear();

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

        explicit Error(
            data::Symbol kind = typeKind<Error>(),
            const std::string &what = "Unspecified Error") noexcept
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

        void toThreadLastError() {
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