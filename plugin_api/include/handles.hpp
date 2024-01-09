#pragma once

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
    };

    /**
     * Replace uses of StringOrd with Symbol
     */
    using StringOrd = Symbol;

    //
    // All objects are passed by handle, this class abstracts the object handles.
    // The main categories of objects are Containers, Scopes, and Subscriptions.
    //
    class ObjHandle {
    protected:
        uint32_t _handle{0};
        void required() const {
            if(_handle == 0) {
                throw std::runtime_error("Handle is required");
            }
        }

    public:
        constexpr ObjHandle() noexcept = default;
        constexpr ObjHandle(const ObjHandle &) noexcept = default;
        constexpr ObjHandle(ObjHandle &&) noexcept = default;
        constexpr ObjHandle &operator=(const ObjHandle &) noexcept = default;
        constexpr ObjHandle &operator=(ObjHandle &&) noexcept = default;
        ~ObjHandle() noexcept = default;

        explicit constexpr ObjHandle(uint32_t handle) noexcept : _handle{handle} {
        }

        constexpr bool operator==(ObjHandle other) const noexcept {
            return _handle == other._handle;
        }

        constexpr bool operator!=(ObjHandle other) const noexcept {
            return _handle != other._handle;
        }

        constexpr explicit operator bool() const noexcept {
            return _handle != 0;
        }

        constexpr bool operator!() const noexcept {
            return _handle == 0;
        }

        //
        // Retrieve underlying handle ID - this is should never be used directly.
        //
        [[nodiscard]] constexpr uint32_t getHandleId() const noexcept {
            return _handle;
        }

        //
        // Allows a handle to be released early.
        //
        void release() const {
            required();
            callApi([*this]() { ::ggapiReleaseHandle(_handle); });
        }

        //
        // Detaches underlying handle, cancelling any side effects such as auto-releasing.
        //
        void detach() noexcept {
            _handle = 0;
        }

        //
        // Checks if this object is the same as the other even if the handles are different.
        // May throw if either handle no longer is valid.
        //
        [[nodiscard]] bool isSameObject(ObjHandle other) const {
            return *this == other || callApiReturn<bool>([*this, other]() {
                return ::ggapiIsSameObject(_handle, other._handle);
            });
        }

        [[nodiscard]] bool isTask() const {
            return ::ggapiIsTask(getHandleId());
        }

        [[nodiscard]] bool isScope() const {
            return ::ggapiIsScope(getHandleId());
        }

        [[nodiscard]] bool isSubscription() const {
            return ::ggapiIsSubscription(getHandleId());
        }

        [[nodiscard]] bool isStruct() const {
            return ::ggapiIsStruct(getHandleId());
        }

        [[nodiscard]] bool isList() const {
            return ::ggapiIsList(getHandleId());
        }

        [[nodiscard]] bool isBuffer() const {
            return ::ggapiIsBuffer(getHandleId());
        }

        [[nodiscard]] bool isContainer() const {
            return ::ggapiIsContainer(getHandleId());
        }

        [[nodiscard]] bool isScalar() const {
            return ::ggapiIsScalar(getHandleId());
        }

        [[nodiscard]] bool isChannel() const {
            return ::ggapiIsChannel(getHandleId());
        }
    };

} // namespace ggapi
