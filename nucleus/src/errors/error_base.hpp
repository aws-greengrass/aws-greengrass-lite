#pragma once
#include "data/string_table.hpp"
#include <cpp_api.hpp>
#include <error_tmpl.hpp>
#include <optional>
#include <type_traits>

namespace ggapi {
    class Symbol;
} // namespace ggapi

namespace errors {

    namespace traits {
        struct ErrorTraits {
            using SymbolType = data::Symbol;
            static SymbolType translateKind(std::string_view strKind) noexcept;
            static SymbolType translateKind(SymbolType symKind) noexcept;
            static SymbolType translateKind(ggapi::Symbol symKind) noexcept;
            static SymbolType translateKind(ggapiErrorKind intKind) noexcept;
        };
    } // namespace traits

    using Error = util::ErrorBase<traits::ErrorTraits>;

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

        [[nodiscard]] static uint32_t fetchKindAsInt();

    public:
        [[nodiscard]] bool hasError() const noexcept {
            return getKindAsInt() != 0;
        }

        [[nodiscard]] ggapiErrorKind getKindAsInt() const {
            if(_kindSymbolId >= 0) {
                return static_cast<uint32_t>(_kindSymbolId);
            } else {
                return fetchKindAsInt();
            }
        }

        [[nodiscard]] const char *getCachedWhat() const;

        [[nodiscard]] std::optional<Error> getError() const;

        ggapiErrorKind setError(const Error &error);

        void clear();
        void reset() noexcept;

        /**
         * Retrieve the ThreadErrorContainer self.
         */
        static ThreadErrorContainer &get() {
            static thread_local ThreadErrorContainer container;
            return container;
        }
    };

} // namespace errors

namespace util {

    template<>
    inline ggapiErrorKind ErrorBase<errors::traits::ErrorTraits>::toThreadLastError() const {
        return errors::ThreadErrorContainer::get().setError(*this);
    }

} // namespace util
