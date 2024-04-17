#pragma once

#include "api_forwards.hpp"
#include "c_api.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace ggapi {

    /**
     * Wraps a string ordinal as consumer of the APIs
     *
     * The constructors will typically be used before a module is fully initialized
     * ggapiGetSymbol is expected to only fail if out of memory, and we'll
     * consider that unrecoverable
     */
    class Symbol {
    private:
        uint32_t _asInt{0};

    public:
        static uint32_t intern(std::string_view sv) noexcept {
            uint32_t r = ::ggapiGetSymbol(sv.data(), sv.length());
            if(r == 0) {
                std::terminate();
            }
            return r;
        }

        // NOLINTNEXTLINE(google-explicit-constructor)
        Symbol(const std::string &sv) noexcept : _asInt{intern(sv)} {
        }

        // NOLINTNEXTLINE(google-explicit-constructor)
        Symbol(std::string_view sv) noexcept : _asInt{intern(sv)} {
        }

        // NOLINTNEXTLINE(google-explicit-constructor)
        Symbol(const char *sv) noexcept : _asInt{intern(sv)} {
        }

        explicit constexpr Symbol(uint32_t internedVal) noexcept : _asInt{internedVal} {
        }

        constexpr Symbol() noexcept = default;
        constexpr Symbol(const Symbol &) noexcept = default;
        constexpr Symbol(Symbol &&) noexcept = default;
        constexpr Symbol &operator=(const Symbol &) noexcept = default;
        constexpr Symbol &operator=(Symbol &&) noexcept = default;
        ~Symbol() noexcept = default;

        constexpr bool operator==(Symbol other) const noexcept {
            return _asInt == other._asInt;
        }

        constexpr bool operator!=(Symbol other) const noexcept {
            return _asInt != other._asInt;
        }

        constexpr explicit operator bool() const noexcept {
            return _asInt != 0;
        }

        constexpr bool operator!() const noexcept {
            return _asInt == 0;
        }

        [[nodiscard]] constexpr uint32_t asInt() const noexcept {
            return _asInt;
        }

        [[nodiscard]] std::string toString() const {
            ggapiMaxLen len = 0;
            callApiThrowError(::ggapiGetSymbolStringLen, _asInt, &len);
            return stringFillHelper(len, [*this](auto buf, auto bufLen, auto fillLen, auto reqLen) {
                callApiThrowError(::ggapiGetSymbolString, _asInt, buf, bufLen, fillLen, reqLen);
            });
        }

        [[nodiscard]] explicit operator std::string() const {
            return toString();
        }
    };

    /**
     * Replace uses of StringOrd with Symbol
     */
    using StringOrd = Symbol;

    /**
     * Use std::shared-ptr to take care of ref-counting. This can be performance-improved in future.
     */
    class HandleIndirect {
        uint32_t _handle{0};

    public:
        explicit HandleIndirect(uint32_t handleId) : _handle(handleId) {
        }
        HandleIndirect() = delete;
        HandleIndirect(const HandleIndirect &) = delete;
        HandleIndirect &operator=(const HandleIndirect &) = delete;
        HandleIndirect(HandleIndirect &&) = delete;
        HandleIndirect &operator=(HandleIndirect &&) = delete;
        ~HandleIndirect() noexcept {
            // If handle is invalid, cannot throw
            ::ggapiReleaseHandle(_handle);
        }

        [[nodiscard]] ggapiObjHandle asId() const noexcept {
            return _handle;
        }

        [[nodiscard]] std::shared_ptr<HandleIndirect> duplicate() const {
            ggapiObjHandle retHandle = 0;
            callApiThrowError(::ggapiDupHandle, _handle, &retHandle);
            return std::make_shared<HandleIndirect>(retHandle);
        }

        [[nodiscard]] ggapiObjHandle makeTemp() const {
            ggapiObjHandle retHandle = 0;
            callApiThrowError(::ggapiTempHandle, _handle, &retHandle);
            return retHandle;
        }

        static std::shared_ptr<HandleIndirect> of(ggapiObjHandle handle) {
            if(handle) {
                // Nucleus gives a handle for Plugin to "own", contract is that when Plugin is done
                // with it, it must explicitly call release.
                return std::make_shared<HandleIndirect>(handle);
            } else {
                return {};
            }
        }

        static ggapiObjHandle idOf(const std::shared_ptr<HandleIndirect> &ptr) noexcept {
            if(ptr) {
                return ptr->asId();
            } else {
                return 0;
            }
        }
    };
    using SharedHandle = std::shared_ptr<HandleIndirect>;

    /**
     * All objects are passed by handle, this class abstracts the object handles.
     * The main categories of objects are Containers, Scopes, and Subscriptions.
     */
    class ObjHandle {
        SharedHandle _handle{};

    protected:
        void required() const {
            if(!_handle) {
                throw std::runtime_error("Handle is required");
            }
        }

    public:
        [[nodiscard]] static bool isA(const ObjHandle &obj) {
            return true;
        }

        constexpr ObjHandle() noexcept = default;
        ObjHandle(const ObjHandle &) noexcept = default;
        ObjHandle(ObjHandle &&) noexcept = default;
        ObjHandle &operator=(const ObjHandle &) noexcept = default;
        ObjHandle &operator=(ObjHandle &&) noexcept = default;
        ~ObjHandle() noexcept = default;

        explicit ObjHandle(SharedHandle handle) noexcept : _handle{std::move(handle)} {
        }

        /**
         * Convert integer handle to tracked handle
         * @tparam T type of tracked handle
         * @param h integer handle returned by Nucleus
         * @return tracked handle
         */
        template<typename T = ObjHandle>
        [[nodiscard]] static T of(uint32_t h) {
            static_assert(std::is_base_of_v<ObjHandle, T>);
            SharedHandle shared = HandleIndirect::of(h);
            return T(shared);
        }

        bool operator==(const ObjHandle &other) const noexcept {
            return asId() == other.asId();
        }

        bool operator!=(const ObjHandle &other) const noexcept {
            return asId() != other.asId();
        }

        explicit operator bool() const noexcept {
            return asId() != 0;
        }

        bool operator!() const noexcept {
            return asId() == 0;
        }

        /**
         * Retrieve underlying Nucleus handle ID
         */
        [[nodiscard]] uint32_t asId() const noexcept {
            return HandleIndirect::idOf(_handle);
        }

        /**
         * Retrieve underlying Nucleus handle ID (older use)
         */
        [[nodiscard]] uint32_t getHandleId() const noexcept {
            return asId();
        }

        /**
         * Release reference to handle
         */
        void reset() {
            _handle.reset();
        }

        /**
         * close - interpretation depends on type of object, by default it's an alias of reset()
         */
        void close() {
            if(*this) {
                callApiThrowError(::ggapiCloseHandle, asId());
                reset();
            }
        }

        /**
         * Checks if this object is the same as the other even if the handles are different.
         * May throw if either handle no longer is valid.
         */
        [[nodiscard]] bool isSameObject(const ObjHandle &other) const {
            auto left = asId();
            auto right = other.asId();
            return left == right || callApiReturn<bool>([left, right]() {
                       return ::ggapiIsSameObject(left, right);
                   });
        }

        [[nodiscard]] bool isPromise() const {
            return callBoolApiThrowError(::ggapiIsPromise, asId());
        }

        [[nodiscard]] bool isFuture() const {
            return callBoolApiThrowError(::ggapiIsFuture, asId());
        }

        [[nodiscard]] bool isScope() const {
            return callBoolApiThrowError(::ggapiIsScope, asId());
        }

        [[nodiscard]] bool isSubscription() const {
            return callBoolApiThrowError(::ggapiIsSubscription, asId());
        }

        [[nodiscard]] bool isStruct() const {
            return callBoolApiThrowError(::ggapiIsStruct, asId());
        }

        [[nodiscard]] bool isList() const {
            return callBoolApiThrowError(::ggapiIsList, asId());
        }

        [[nodiscard]] bool isBuffer() const {
            return callBoolApiThrowError(::ggapiIsBuffer, asId());
        }

        [[nodiscard]] bool isContainer() const {
            return callBoolApiThrowError(::ggapiIsContainer, asId());
        }

        [[nodiscard]] bool isScalar() const {
            return callBoolApiThrowError(::ggapiIsScalar, asId());
        }

        [[nodiscard]] bool isChannel() const {
            return callBoolApiThrowError(::ggapiIsChannel, asId());
        }

        template<typename T>
        [[nodiscard]] T duplicate() const {
            if(_handle) {
                return T(_handle->duplicate());
            } else {
                return T();
            }
        }

        [[nodiscard]] ggapiObjHandle makeTemp() const {
            if(_handle) {
                return _handle->makeTemp();
            } else {
                return 0;
            }
        }
    };

} // namespace ggapi
