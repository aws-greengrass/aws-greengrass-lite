#pragma once

#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

extern "C" {
#include "c_api.h"
}

//
// Sugar around the c-api to make cpp development easier
//
namespace ggapi {

    class StringOrd;
    class ObjHandle;
    class Container;
    class Struct;
    class List;
    class Buffer;
    class Scope;
    class Task;
    class ModuleScope;
    class Subscription;
    class GgApiError; // error from GG API call

    using topicCallbackLambda = std::function<Struct(Scope, StringOrd, Struct)>;
    using lifecycleCallbackLambda = std::function<void(Scope, StringOrd, Struct)>;
    using topicCallback_t = Struct(*)(Task, StringOrd, Struct);
    using lifecycleCallback_t = void(*)(ModuleScope, StringOrd, Struct);
    uint32_t topicCallbackProxy(
        uintptr_t callbackContext,
        uint32_t taskHandle,
        uint32_t topicOrd,
        uint32_t dataStruct) noexcept;
    bool lifecycleCallbackProxy(
        uintptr_t callbackContext,
        uint32_t moduleHandle,
        uint32_t phaseOrd,
        uint32_t dataStruct) noexcept;
    template<typename T>
    T trapErrorReturn(const std::function<T()> &fn) noexcept;
    uint32_t trapErrorReturnHandle(const std::function<ObjHandle()> &fn) noexcept;
    uint32_t trapErrorReturnOrd(const std::function<StringOrd()> &fn) noexcept;
    template<typename T>
    T callApiReturn(const std::function<T()> &fn);
    template<typename T>
    T callApiReturnHandle(const std::function<uint32_t()> &fn);
    StringOrd callApiReturnOrd(const std::function<uint32_t()> &fn);
    void callApi(const std::function<void()> &fn);

    // Helper functions for consistent string copy pattern
    inline std::string stringFillHelper(
        size_t strLen, const std::function<size_t(char *, size_t)> &stringFillFn) {
        if(strLen == 0) {
            return {};
        }
        std::vector<char> buffer(strLen + 1);
        size_t finalLen = stringFillFn(buffer.data(), buffer.size());
        return {buffer.data(), finalLen};
    }

    //
    // Wraps a string ordinal as consumer of the APIs
    //
    // The constructors will typically be used before a module is fully initialized
    // ggapiGetStringOrdinal is expected to only fail if out of memory, and we'll
    // consider that unrecoverable
    //
    class StringOrd {
    private:
        uint32_t _ord{0};

    public:
        static uint32_t intern(std::string_view sv) noexcept {
            uint32_t r = ::ggapiGetStringOrdinal(sv.data(), sv.length());
            if(r == 0) {
                std::terminate();
            }
            return r;
        }

        // NOLINTNEXTLINE(google-explicit-constructor)
        StringOrd(const std::string &sv) noexcept : _ord{intern(sv)} {
        }

        // NOLINTNEXTLINE(google-explicit-constructor)
        StringOrd(std::string_view sv) noexcept : _ord{intern(sv)} {
        }

        // NOLINTNEXTLINE(google-explicit-constructor)
        StringOrd(const char *sv) noexcept : _ord{intern(sv)} {
        }

        explicit constexpr StringOrd(uint32_t ord) noexcept : _ord{ord} {
        }

        constexpr StringOrd() noexcept = default;
        constexpr StringOrd(const StringOrd &) noexcept = default;
        constexpr StringOrd(StringOrd &&) noexcept = default;
        constexpr StringOrd &operator=(const StringOrd &) noexcept = default;
        constexpr StringOrd &operator=(StringOrd &&) noexcept = default;
        ~StringOrd() noexcept = default;

        constexpr bool operator==(StringOrd other) const noexcept {
            return _ord == other._ord;
        }

        constexpr bool operator!=(StringOrd other) const noexcept {
            return _ord != other._ord;
        }

        [[nodiscard]] constexpr uint32_t toOrd() const noexcept {
            return _ord;
        }

        [[nodiscard]] std::string toString() const {
            auto len =
                callApiReturn<size_t>([*this]() { return ::ggapiGetOrdinalStringLen(_ord); });
            return stringFillHelper(len, [*this](auto buf, auto bufLen) {
                return callApiReturn<size_t>(
                    [*this, &buf, bufLen]() { return ::ggapiGetOrdinalString(_ord, buf, bufLen); });
            });
        }
    };

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
    };

    //
    // A task handle represents an active LPC operation. The handle is deleted after the completion
    // callback (if any).
    //
    class Task : public ObjHandle {
        void check() {
            if(getHandleId() != 0 && !ggapiIsTask(getHandleId())) {
                throw std::runtime_error("Task handle expected");
            }
        }

    public:
        explicit Task(const ObjHandle &other) : ObjHandle{other} {
            check();
        }

        explicit Task(uint32_t handle) : ObjHandle{handle} {
            check();
        }

        //
        // Create an asynchronous LPC call - returning the Task handle for the call.
        //
        [[nodiscard]] static Task sendToTopicAsync(
            StringOrd topic, Struct message, topicCallback_t result, int32_t timeout = -1);

        //
        // Create a synchronous LPC call - a task handle is created, and observable by subscribers
        // however the task is deleted by the time the call returns. Most handlers are called in
        // the same (callers) thread, however this must not be assumed.
        //
        [[nodiscard]] static Struct sendToTopic(
            StringOrd topic, Struct message, int32_t timeout = -1);

        //
        // Block until task completes including final callback if there is one.
        //
        [[nodiscard]] Struct waitForTaskCompleted(int32_t timeout = -1);

        //
        // Cancel task - if a callback is executing, it will complete first.
        //
        void cancelTask();

        //
        // When in a task callback, returns the associated task.
        //
        [[nodiscard]] static Task current();
    };

    //
    // Subscription handles indicate an active listener for LPC topics. Anonymous listeners
    // can also exist.
    //
    class Subscription : public ObjHandle {
        void check() {
            if(getHandleId() != 0 && !ggapiIsSubscription(getHandleId())) {
                throw std::runtime_error("Subscription handle expected");
            }
        }

    public:
        explicit Subscription(const ObjHandle &other) : ObjHandle{other} {
            check();
        }

        explicit Subscription(uint32_t handle) : ObjHandle{handle} {
            check();
        }

        //
        // Send a message to this specific subscription. Return immediately.
        //
        [[nodiscard]] Task callAsync(
            Struct message, topicCallback_t result, int32_t timeout = -1) const;

        //
        // Send a message to this specific subscription. If possible, run in same thread.
        // Block until completion.
        //
        [[nodiscard]] Struct call(Struct message, int32_t timeout = -1) const;
    };

    //
    // Scopes are a class of handles that are used as targets for anchoring other handles.
    // See the subclasses to understand the specific types of scopes.
    //
    class Scope : public ObjHandle {

        void check() {
            if(getHandleId() != 0 && !ggapiIsScope(getHandleId())) {
                throw std::runtime_error("Scope handle expected");
            }
        }

    public:
        Scope() noexcept = default;
        Scope(const Scope &) noexcept = default;
        Scope(Scope &&) noexcept = default;
        Scope &operator=(const Scope &) noexcept = default;
        Scope &operator=(Scope &&) noexcept = default;
        ~Scope() = default;

        explicit Scope(const ObjHandle &other) : ObjHandle(other) {
            check();
        }

        explicit Scope(uint32_t handle) : ObjHandle(handle) {
            check();
        }

        //
        // Creates a subscription. A subscription is tied to scope and will be unsubscribed if
        // the scope is deleted.
        //
        [[nodiscard]] Subscription subscribeToTopic(StringOrd topic, topicCallback_t callback);

        //
        // Anchor an object against this scope.
        //
        template<typename T>
        [[nodiscard]] T anchor(T otherHandle) const;
    };

    //
    // Module scope. For module-global data.
    //
    class ModuleScope : public Scope {
    public:
        ModuleScope() noexcept = default;
        ModuleScope(const ModuleScope &) noexcept = default;
        ModuleScope(ModuleScope &&) noexcept = default;
        ModuleScope &operator=(const ModuleScope &) noexcept = default;
        ModuleScope &operator=(ModuleScope &&) noexcept = default;
        ~ModuleScope() = default;

        explicit ModuleScope(const ObjHandle &other) : Scope{other} {
        }

        explicit ModuleScope(uint32_t handle) : Scope{handle} {
        }

        [[nodiscard]] ModuleScope registerPlugin(
            StringOrd componentName, lifecycleCallback_t callback);
    };

    /**
     * Temporary (stack-local) scope, that is default scope for objects.
     */
    class CallScope : public Scope {

    public:
        /**
         * Use only in stack context, push and create a stack-local call scope
         * that is popped when object is destroyed.
         */
        explicit CallScope() : Scope() {
            _handle = callApiReturn<uint32_t>([]() { return ::ggapiCreateCallScope(); });
        }

        CallScope(const CallScope &) = delete;
        CallScope &operator=(const CallScope &) = delete;
        CallScope(CallScope &&) noexcept = delete;
        CallScope &operator=(CallScope &&) noexcept = delete;

        void release() noexcept {
            if(_handle) {
                ::ggapiReleaseHandle(_handle); // do not (re)throw exception
                _handle = 0;
            }
        }

        ~CallScope() noexcept {
            release();
        }

        static Scope newCallScope() {
            return callApiReturnHandle<Scope>([]() { return ::ggapiCreateCallScope(); });
        }

        static Scope current() {
            return callApiReturnHandle<Scope>([]() { return ::ggapiGetCurrentCallScope(); });
        }
    };

    //
    // Containers are the root for Structures and Lists
    //
    class Container : public ObjHandle {
    private:
    public:
        // Value can only be used for parameter values
        using Value = std::variant<
            bool,
            int64_t,
            uint64_t,
            double,
            std::string,
            std::string_view,
            const char *,
            ObjHandle,
            StringOrd>;
        using KeyValue = std::pair<StringOrd, Value>;

        explicit Container(const ObjHandle &other) : ObjHandle{other} {
        }

        explicit Container(uint32_t handle) : ObjHandle{handle} {
        }

        [[nodiscard]] uint32_t size() const {
            return callApiReturn<uint32_t>([*this]() { return ::ggapiGetSize(_handle); });
        }
    };

    //
    // Structures are containers with associative keys
    //
    class Struct : public Container {
        void check() {
            if(getHandleId() != 0 && !ggapiIsStruct(getHandleId())) {
                throw std::runtime_error("Structure handle expected");
            }
        }

    public:
        explicit Struct(const ObjHandle &other) : Container{other} {
            check();
        }

        explicit Struct(uint32_t handle) : Container{handle} {
            check();
        }

        static Struct create() {
            return Struct(::ggapiCreateStruct());
        }

        template<typename T>
        Struct &put(StringOrd ord, T v) {
            required();
            if constexpr(std::is_same_v<bool, T>) {
                callApi([*this, ord, v]() { ::ggapiStructPutBool(_handle, ord.toOrd(), v); });
            } else if constexpr(std::is_integral_v<T>) {
                auto intv = static_cast<uint64_t>(v);
                callApi(
                    [*this, ord, intv]() { ::ggapiStructPutInt64(_handle, ord.toOrd(), intv); });
            } else if constexpr(std::is_floating_point_v<T>) {
                auto floatv = static_cast<double>(v);
                callApi([*this, ord, floatv]() {
                    ::ggapiStructPutFloat64(_handle, ord.toOrd(), floatv);
                });
            } else if constexpr(std::is_base_of_v<StringOrd, T>) {
                callApi([*this, ord, v]() {
                    ::ggapiStructPutStringOrd(_handle, ord.toOrd(), v.toOrd());
                });
            } else if constexpr(std::is_constructible_v<std::string_view, T>) {
                std::string_view str(v);
                callApi([*this, ord, str]() {
                    ::ggapiStructPutString(_handle, ord.toOrd(), str.data(), str.length());
                });
            } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
                callApi([*this, ord, &v]() {
                    ::ggapiStructPutHandle(_handle, ord.toOrd(), v.getHandleId());
                });
            } else if constexpr(std::is_same_v<Value, T>) {
                std::visit([this, ord](auto &&value) { this->put(ord, value); }, v);
            } else {
                static_assert(T::usingUnsupportedType, "Unsupported type");
            }
            return *this;
        }

        Struct &put(const KeyValue &kv) {
            return put(kv.first, kv.second);
        }

        Struct &put(std::initializer_list<KeyValue> list) {
            for(const auto &i : list) {
                put(i);
            }
            return *this;
        }

        [[nodiscard]] bool hasKey(StringOrd ord) const {
            required();
            return callApiReturn<bool>(
                [*this, ord]() { return ::ggapiStructHasKey(_handle, ord.toOrd()); });
        }

        template<typename T>
        T get(StringOrd ord) const {
            required();
            if constexpr(std::is_same_v<bool, T>) {
                return callApiReturn<bool>(
                    [*this, ord]() { return ::ggapiStructGetBool(_handle, ord.toOrd()); });
            } else if constexpr(std::is_integral_v<T>) {
                auto intv = callApiReturn<uint64_t>(
                    [*this, ord]() { return ::ggapiStructGetInt64(_handle, ord.toOrd()); });
                return static_cast<T>(intv);
            } else if constexpr(std::is_floating_point_v<T>) {
                auto floatv = callApiReturn<double>(
                    [*this, ord]() { return ::ggapiStructGetFloat64(_handle, ord.toOrd()); });
                return static_cast<T>(floatv);
            } else if constexpr(std::is_base_of_v<std::string, T>) {
                size_t len = callApiReturn<size_t>(
                    [*this, ord]() { return ::ggapiStructGetStringLen(_handle, ord.toOrd()); });
                return stringFillHelper(len, [*this, ord](auto buf, auto bufLen) {
                    return callApiReturn<size_t>([*this, ord, &buf, bufLen]() {
                        return ::ggapiStructGetString(_handle, ord.toOrd(), buf, bufLen);
                    });
                });
            } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
                return callApiReturnHandle<T>(
                    [*this, ord]() { return ::ggapiStructGetHandle(_handle, ord.toOrd()); });
            } else {
                static_assert(T::usingUnsupportedType, "Unsupported type");
            }
        }

        template<typename T>
        T getValue(const std::initializer_list<std::string_view> &keys) const {
            ggapi::Struct childStruct = *this;
            auto it = keys.begin();
            for(; it != std::prev(keys.end()); it++) {
                childStruct = childStruct.get<ggapi::Struct>(*it);
            }
            return childStruct.get<T>(*it);
        }
    };

    //
    // Lists are containers with index-based keys
    //
    class List : public Container {
        void check() {
            if(getHandleId() != 0 && !ggapiIsList(getHandleId())) {
                throw std::runtime_error("List handle expected");
            }
        }

    public:
        explicit List(const ObjHandle &other) : Container{other} {
            check();
        }

        explicit List(uint32_t handle) : Container{handle} {
            check();
        }

        static List create() {
            return List(::ggapiCreateList());
        }

        template<typename T>
        List &put(int32_t idx, T v) {
            required();
            if constexpr(std::is_same_v<bool, T>) {
                callApi([*this, idx, v]() { ::ggapiListPutBool(_handle, idx, v); });
            } else if constexpr(std::is_integral_v<T>) {
                auto intv = static_cast<uint64_t>(v);
                callApi([*this, idx, intv]() { ::ggapiListPutInt64(_handle, idx, intv); });
            } else if constexpr(std::is_floating_point_v<T>) {
                auto floatv = static_cast<double>(v);
                callApi([*this, idx, floatv]() { ::ggapiListPutFloat64(_handle, idx, floatv); });
            } else if constexpr(std::is_base_of_v<StringOrd, T>) {
                callApi([*this, idx, v]() { ::ggapiListPutStringOrd(_handle, idx, v.toOrd()); });
            } else if constexpr(std::is_constructible_v<std::string_view, T>) {
                std::string_view str(v);
                callApi([*this, idx, str]() {
                    ::ggapiListPutString(_handle, idx, str.data(), str.length());
                });
            } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
                callApi(
                    [*this, idx, &v]() { ::ggapiListPutHandle(_handle, idx, v.getHandleId()); });
            } else if constexpr(std::is_same_v<Value, T>) {
                std::visit([this, idx](auto &&value) { put(idx, value); }, v);
            } else {
                static_assert(T::usingUnsupportedType, "Unsupported type");
            }
            return *this;
        }

        template<typename T>
        List &insert(int32_t idx, T v) {
            required();
            if constexpr(std::is_same_v<bool, T>) {
                callApi([*this, idx, v]() { ::ggapiListInsertBool(_handle, idx, v); });
            } else if constexpr(std::is_integral_v<T>) {
                auto intv = static_cast<uint64_t>(v);
                callApi([*this, idx, intv]() { ::ggapiListInsertInt64(_handle, idx, intv); });
            } else if constexpr(std::is_floating_point_v<T>) {
                auto floatv = static_cast<double>(v);
                callApi([*this, idx, floatv]() { ::ggapiListInsertFloat64(_handle, idx, floatv); });
            } else if constexpr(std::is_base_of_v<StringOrd, T>) {
                callApi([*this, idx, v]() { ::ggapiListInsertStringOrd(_handle, idx, v.toOrd()); });
            } else if constexpr(std::is_constructible_v<std::string_view, T>) {
                std::string_view str(v);
                callApi([*this, idx, str]() {
                    ::ggapiListInsertString(_handle, idx, str.data(), str.length());
                });
            } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
                callApi(
                    [*this, idx, &v]() { ::ggapiListInsertHandle(_handle, idx, v.getHandleId()); });
            } else if constexpr(std::is_same_v<Value, T>) {
                std::visit([this, idx](auto &&value) { insert(idx, value); }, v);
            } else {
                static_assert(T::usingUnsupportedType, "Unsupported type");
            }
            return *this;
        }

        List &append(const Value &value) {
            required();
            std::visit([this](auto &&value) { insert(-1, value); }, value);
            return *this;
        }

        List &append(std::initializer_list<Value> list) {
            required();
            for(const auto &i : list) {
                append(i);
            }
            return *this;
        }

        template<typename T>
        T get(int32_t idx) {
            required();
            if constexpr(std::is_same_v<bool, T>) {
                return callApiReturn<bool>(
                    [*this, idx]() { return ::ggapiListGetBool(_handle, idx); });
            } else if constexpr(std::is_integral_v<T>) {
                auto intv = callApiReturn<uint64_t>(
                    [*this, idx]() { return ::ggapiListGetInt64(_handle, idx); });
                return static_cast<T>(intv);
            } else if constexpr(std::is_floating_point_v<T>) {
                auto floatv = callApiReturn<double>(
                    [*this, idx]() { return ::ggapiListGetFloat64(_handle, idx); });
                return static_cast<T>(floatv);
            } else if constexpr(std::is_base_of_v<std::string, T>) {
                size_t len = callApiReturn<size_t>(
                    [*this, idx]() { return ::ggapiListGetStringLen(_handle, idx); });
                return stringFillHelper(len, [*this, idx](auto buf, auto bufLen) {
                    return callApiReturn<size_t>([*this, idx, &buf, bufLen]() {
                        return ::ggapiListGetString(_handle, idx, buf, bufLen);
                    });
                });
            } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
                return callApiReturnHandle<T>(
                    [*this, idx]() { return ::ggapiListGetHandle(_handle, idx); });
            } else {
                static_assert(T::usingUnsupportedType, "Unsupported type");
            }
        }
    };

    class BufferStream;
    class BufferInStream;
    class BufferOutStream;

    //
    // Buffers are shared containers of bytes
    //
    class Buffer : public Container {
        void check() {
            if(getHandleId() != 0 && !ggapiIsBuffer(getHandleId())) {
                throw std::runtime_error("Buffer handle expected");
            }
        }

    public:
        explicit Buffer(const ObjHandle &other) : Container{other} {
            check();
        }

        explicit Buffer(uint32_t handle) : Container{handle} {
            check();
        }

        static Buffer create() {
            return Buffer(::ggapiCreateBuffer());
        }

        BufferStream stream();
        BufferInStream in();
        BufferOutStream out();

        Buffer &put(int32_t idx, const char *data, size_t n) {
            required();
            if(n > std::numeric_limits<uint32_t>::max()) {
                throw std::out_of_range("length out of range");
            }
            callApi([*this, idx, data, n]() { ::ggapiBufferPut(_handle, idx, data, n); });
            return *this;
        }

        template<typename T>
        Buffer &put(int32_t idx, const T *data, size_t n) {
            required();
            // NOLINTNEXTLINE(*-type-reinterpret-cast)
            const char *d = reinterpret_cast<const char *>(data);
            size_t nn = n * sizeof(const char);
            if(nn < n) {
                throw std::out_of_range("length out of range");
            }
            return put(idx, d, nn);
        }

        template<typename T>
        Buffer &put(int32_t idx, const std::vector<T> &vec) {
            return put(idx, vec.data(), vec.size());
        }

        template<typename T>
        Buffer &put(int32_t idx, const std::basic_string_view<T> sv) {
            return put(idx, sv.data(), sv.length());
        }

        Buffer &insert(int32_t idx, const char *data, size_t n) {
            required();
            if(n > std::numeric_limits<uint32_t>::max()) {
                throw std::out_of_range("length out of range");
            }
            callApi([*this, idx, data, n]() { ::ggapiBufferInsert(_handle, idx, data, n); });
            return *this;
        }

        template<typename T>
        Buffer &insert(int32_t idx, const T *data, size_t n) {
            required();
            // NOLINTNEXTLINE(*-type-reinterpret-cast)
            const char *d = reinterpret_cast<const char *>(data);
            size_t nn = n * sizeof(const char);
            if(nn < n) {
                throw std::out_of_range("length out of range");
            }
            return insert(idx, d, nn);
        }

        template<typename T>
        Buffer &insert(int32_t idx, const std::vector<T> &vec) {
            return insert(idx, vec.data(), vec.size());
        }

        template<typename T>
        Buffer &insert(int32_t idx, const std::basic_string_view<T> sv) {
            return insert(idx, sv.data(), sv.length());
        }

        size_t get(int32_t idx, char *data, size_t max) {
            required();
            if(max > std::numeric_limits<uint32_t>::max()) {
                throw std::out_of_range("max length out of range");
            }
            return callApiReturn<size_t>([*this, idx, data, max]() -> size_t {
                return ::ggapiBufferGet(_handle, idx, data, max);
            });
        }

        template<typename T>
        size_t get(int32_t idx, T *data, size_t max) {
            required();
            // NOLINTNEXTLINE(*-type-reinterpret-cast)
            char *d = reinterpret_cast<char *>(data);
            size_t nn = max * sizeof(const char);
            if(nn < max) {
                throw std::out_of_range("length out of range");
            }
            size_t act = get(idx, d, nn);
            return act / sizeof(T);
        }

        template<typename T>
        size_t get(int32_t idx, std::vector<T> &vec) {
            size_t actual = get(idx, vec.data(), vec.size());
            vec.resize(actual);
            return actual;
        }

        template<typename T>
        size_t get(int32_t idx, std::basic_string<T> &str) {
            size_t actual = get(idx, str.data(), str.size());
            str.resize(actual);
            return actual;
        }

        template<typename T>
        T get(int32_t idx, size_t max) {
            if(max > std::numeric_limits<uint32_t>::max()) {
                throw std::out_of_range("max length out of range");
            }
            T buffer;
            buffer.resize(max);
            get(idx, buffer);
            return buffer;
        }

        Buffer &resize(uint32_t newSize) {
            required();
            callApi([*this, newSize]() { ::ggapiBufferResize(_handle, newSize); });
            return *this;
        }
    };

    class BufferStream : public std::streambuf {
        //
        // Lightweight implementation to allow << and >> operators
        //

        Buffer _buffer;
        pos_type _inpos{0}; // unbuffered position
        pos_type _outpos{0};
        std::vector<char> _inbuf;
        std::vector<char> _outbuf;
        static constexpr uint32_t BUFFER_SIZE{256};

        int32_t inAsInt(uint32_t limit = std::numeric_limits<int32_t>::max()) {
            if(_inpos > limit) {
                throw std::invalid_argument("Seek position beyond limit");
            }
            return static_cast<int32_t>(_inpos);
        }

        int32_t outAsInt(uint32_t limit = std::numeric_limits<int32_t>::max()) {
            if(_outpos > limit) {
                throw std::invalid_argument("Seek position beyond limit");
            }
            return static_cast<int32_t>(_outpos);
        }

        bool readMore() {
            flushRead();
            int32_t pos = inAsInt();
            uint32_t end = _buffer.size();
            if(pos >= end) {
                return false;
            }
            std::vector<char> temp(std::min(end - pos, BUFFER_SIZE));
            auto didRead = _buffer.get(pos, temp);
            if(didRead > 0) {
                _inbuf = std::move(temp);
                // NOLINTNEXTLINE(*-pointer-arithmetic)
                setg(_inbuf.data(), _inbuf.data(), _inbuf.data() + _inbuf.size());
                return true;
            } else {
                return false;
            }
        }

        void flushWrite() {
            if(!_outbuf.empty()) {
                if(unflushed() > 0) {
                    int32_t pos = outAsInt();
                    _buffer.put(pos, pbase(), unflushed());
                    _outpos += unflushed(); // NOLINT(*-narrowing-conversions)
                }
                _outbuf.clear();
                setp(nullptr, nullptr);
            }
        }

        void flushRead() {
            if(!_inbuf.empty()) {
                _inpos += consumed();
                _inbuf.clear();
                setg(nullptr, nullptr, nullptr);
            }
        }

        uint32_t unflushed() {
            return pptr() - pbase(); // NOLINT(*-narrowing-conversions)
        }

        uint32_t unread() {
            return egptr() - gptr(); // NOLINT(*-narrowing-conversions)
        }

        uint32_t consumed() {
            return gptr() - eback(); // NOLINT(*-narrowing-conversions)
        }

        pos_type eInPos() {
            return _inpos + static_cast<pos_type>(consumed());
        }

        void prepareWrite() {
            flushWrite();
            _outbuf.resize(BUFFER_SIZE);
            // NOLINTNEXTLINE(*-pointer-arithmetic)
            setp(_outbuf.data(), _outbuf.data() + _outbuf.size());
        }

        pos_type seek(pos_type cur, off_type pos, std::ios_base::seekdir seekdir) {
            uint32_t end = _buffer.size();
            off_type newPos;

            switch(seekdir) {
                case std::ios_base::beg:
                    newPos = pos;
                    break;
                case std::ios_base::end:
                    newPos = end + pos;
                    break;
                case std::ios_base::cur:
                    newPos = cur + pos;
                    break;
                default:
                    throw std::invalid_argument("Seekdir is invalid");
            }
            if(newPos < 0) {
                newPos = 0;
            }
            if(newPos > end) {
                newPos = end;
            }
            return newPos;
        }

    protected:
        pos_type seekoff(
            off_type pos,
            std::ios_base::seekdir seekdir,
            std::ios_base::openmode openmode) override {
            bool _seekIn = (openmode & std::ios_base::in) != 0;
            bool _seekOut = (openmode & std::ios_base::out) != 0;
            if(_seekIn && _seekOut) {
                flushRead();
                flushWrite();
                _outpos = _inpos = seek(_outpos, pos, seekdir);
                return _outpos;
            }
            if(_seekIn) {
                flushRead();
                _inpos = seek(_inpos, pos, seekdir);
                return _inpos;
            }
            if(_seekOut) {
                flushWrite();
                _outpos = seek(_outpos, pos, seekdir);
                return _outpos;
            }
            return std::streambuf::seekoff(pos, seekdir, openmode);
        }

        pos_type seekpos(pos_type pos, std::ios_base::openmode openmode) override {
            return seekoff(pos, std::ios_base::beg, openmode);
        }

        std::streamsize showmanyc() override {
            pos_type end = _buffer.size();
            pos_type cur = eInPos();

            if(cur >= end) {
                return -1;
            } else {
                return end - cur;
            }
        }

        int underflow() override {
            // called when get buffer underflows
            readMore();
            if(unread() == 0) {
                return traits_type::eof();
            } else {
                return traits_type::to_int_type(*gptr());
            }
        }

        int pbackfail(int_type c) override {
            // called when put-back underflows
            flushRead();
            flushWrite();
            if(eInPos() == 0) {
                return traits_type::eof();
            }
            _inpos -= 1;
            if(traits_type::not_eof(c)) {
                char cc = static_cast<char_type>(c);
                _buffer.put(inAsInt(), std::string_view(&cc, 1));
                return c;
            } else {
                return 0;
            }
        }

        int overflow(int_type c) override {
            // called when buffer full
            prepareWrite(); // make room for data
            std::streambuf::overflow(c);
            if(traits_type::not_eof(c)) {
                // expected to write one character
                *pptr() = static_cast<char_type>(c);
                pbump(1);
            }
            return 0;
        }

        int sync() override {
            flushRead();
            flushWrite();
            return 0;
        }

    public:
        BufferStream(const BufferStream &) = delete;
        BufferStream(BufferStream &&) noexcept = default;
        BufferStream &operator=(const BufferStream &) = delete;
        BufferStream &operator=(BufferStream &&) noexcept = default;

        ~BufferStream() override {
            try {
                flushWrite(); // attempt final flush if omitted
            } catch(...) {
                // destructor not allowed to throw exceptions
            }
        };

        explicit BufferStream(const Buffer buffer) : _buffer(buffer) {
        }
    };

    class BufferInStream : public std::istream {
        BufferStream _stream;

    public:
        explicit BufferInStream(const Buffer buffer) : _stream(buffer), std::istream(&_stream) {
        }
    };

    class BufferOutStream : public std::ostream {
        BufferStream _stream;

    public:
        explicit BufferOutStream(const Buffer buffer) : _stream(buffer), std::ostream(&_stream) {
            _stream.pubseekoff(0, std::ios_base::end, std::ios_base::out);
        }
    };

    inline BufferStream Buffer::stream() {
        return BufferStream(*this);
    }

    inline BufferInStream Buffer::in() {
        return BufferInStream(*this);
    }

    inline BufferOutStream Buffer::out() {
        return BufferOutStream(*this);
    }

    template<typename T>
    inline T Scope::anchor(T otherHandle) const {
        required();
        static_assert(std::is_base_of_v<ObjHandle, T>);
        return callApiReturnHandle<T>(
            [this, otherHandle]() { return ::ggapiAnchorHandle(getHandleId(), otherHandle); });
    }

    inline Subscription Scope::subscribeToTopic(StringOrd topic, topicCallback_t callback) {
        required();
        return callApiReturnHandle<Subscription>([*this, topic, callback]() {
            return ::ggapiSubscribeToTopic(
                getHandleId(),
                topic.toOrd(),
                topicCallbackProxy,
                // TODO: This is undefined behavior; uintptr_t is size of data
                // pointer, not function pointer. This should use static_cast to
                // turn a data pointer to a void*. Will need to store the
                // callback function pointer in some sort of struct
                reinterpret_cast<uintptr_t>(callback) // NOLINT(*-reinterpret-cast)
            );
        });
    }

    inline Task Task::sendToTopicAsync(
        StringOrd topic, Struct message, topicCallback_t result, int32_t timeout) {
        return callApiReturnHandle<Task>([topic, message, result, timeout]() {
            return ::ggapiSendToTopicAsync(
                topic.toOrd(),
                message.getHandleId(),
                topicCallbackProxy,
                reinterpret_cast<uintptr_t>(result), // NOLINT(*-reinterpret-cast)
                timeout);
        });
    }

    inline Struct Task::sendToTopic(ggapi::StringOrd topic, Struct message, int32_t timeout) {
        return callApiReturnHandle<Struct>([topic, message, timeout]() {
            return ::ggapiSendToTopic(topic.toOrd(), message.getHandleId(), timeout);
        });
    }

    inline Task Subscription::callAsync(
        Struct message, topicCallback_t result, int32_t timeout) const {
        required();
        return callApiReturnHandle<Task>([this, message, result, timeout]() {
            return ::ggapiSendToListenerAsync(
                getHandleId(),
                message.getHandleId(),
                topicCallbackProxy,
                reinterpret_cast<uintptr_t>(result), // NOLINT(*-reinterpret-cast)
                timeout);
        });
    }

    inline Struct Subscription::call(Struct message, int32_t timeout) const {
        required();
        return callApiReturnHandle<Struct>([this, message, timeout]() {
            return ::ggapiSendToListener(getHandleId(), message.getHandleId(), timeout);
        });
    }

    inline Struct Task::waitForTaskCompleted(int32_t timeout) {
        required();
        return callApiReturnHandle<Struct>(
            [this, timeout]() { return ::ggapiWaitForTaskCompleted(getHandleId(), timeout); });
    }

    inline void Task::cancelTask() {
        required();
        callApi([this]() { return ::ggapiCancelTask(getHandleId()); });
    }

    inline Task Task::current() {
        return callApiReturnHandle<Task>([]() { return ::ggapiGetCurrentTask(); });
    }

    inline ModuleScope ModuleScope::registerPlugin(
        StringOrd componentName, lifecycleCallback_t callback) {
        required();
        return callApiReturnHandle<ModuleScope>([*this, componentName, callback]() {
            return ::ggapiRegisterPlugin(
                getHandleId(),
                componentName.toOrd(),
                lifecycleCallbackProxy,
                reinterpret_cast<uintptr_t>(callback) // NOLINT(*-reinterpret-cast)
            );
        });
    }

    inline uint32_t topicCallbackProxy(
        uintptr_t callbackContext,
        uint32_t taskHandle,
        uint32_t topicOrd,
        uint32_t dataStruct) noexcept {
        return trapErrorReturn<uint32_t>([callbackContext, taskHandle, topicOrd, dataStruct]() {
            auto callback =
                reinterpret_cast<topicCallback_t>(callbackContext); // NOLINT(*-reinterpret-cast)
            return callback(Task{taskHandle}, StringOrd{topicOrd}, Struct{dataStruct})
                .getHandleId();
        });
    }

    inline bool lifecycleCallbackProxy(
        uintptr_t callbackContext,
        uint32_t moduleHandle,
        uint32_t phaseOrd,
        uint32_t dataStruct) noexcept {
        return trapErrorReturn<bool>([callbackContext, moduleHandle, phaseOrd, dataStruct]() {
            // NOLINTNEXTLINE(*-reinterpret-cast)
            auto callback = reinterpret_cast<lifecycleCallback_t>(callbackContext);
            callback(ModuleScope{moduleHandle}, StringOrd{phaseOrd}, Struct{dataStruct});
            return true;
        });
    }

    //
    // StringOrd constants - done as inline methods so that only used constants are included. Note
    // that ordinal gets are idempotent and thread safe.
    //
    struct Consts {

        static StringOrd error() {
            static const StringOrd error{"error"};
            return error;
        }
    };

    //
    // Translated exception
    //
    class GgApiError : public std::exception {
        StringOrd _ord;

    public:
        constexpr GgApiError(const GgApiError &) noexcept = default;
        constexpr GgApiError(GgApiError &&) noexcept = default;
        GgApiError &operator=(const GgApiError &) noexcept = default;
        GgApiError &operator=(GgApiError &&) noexcept = default;

        explicit GgApiError(const StringOrd &ord) noexcept : _ord{ord} {
        }

        explicit GgApiError(std::string_view errorClass) noexcept : _ord{errorClass} {
        }

        ~GgApiError() override = default;

        constexpr explicit operator StringOrd() const {
            return _ord;
        }

        [[nodiscard]] constexpr StringOrd get() const {
            return _ord;
        }
    };

    //
    // Helper function to throw an exception if there is a thread error
    //
    inline void rethrowOnThreadError() {
        uint32_t errCode = ggapiGetError();
        if(errCode != 0) {
            ggapiSetError(0); // consider error 'handled'
            throw GgApiError(StringOrd(errCode));
        }
    }

    //
    // Exceptions do not cross module borders - translate an exception into a thread error
    //
    template<typename T>
    inline T trapErrorReturn(const std::function<T()> &fn) noexcept {
        try {
            ggapiSetError(0);
            if constexpr(std::is_void_v<T>) {
                fn();
            } else {
                return fn();
            }
        } catch(...) {
            ggapiSetError(Consts::error().toOrd());
            if constexpr(std::is_void_v<T>) {
                return;
            } else {
                return static_cast<T>(0);
            }
        }
    }

    inline void callApi(const std::function<void()> &fn) {
        ggapiSetError(0);
        fn();
        rethrowOnThreadError();
    }

    template<typename T>
    inline T callApiReturn(const std::function<T()> &fn) {
        if constexpr(std::is_void_v<T>) {
            callApi(fn);
        } else {
            ggapiSetError(0);
            T v = fn();
            rethrowOnThreadError();
            return v;
        }
    }

    inline uint32_t trapErrorReturnHandle(const std::function<ObjHandle()> &fn) noexcept {
        return trapErrorReturn<uint32_t>([&fn]() { return fn().getHandleId(); });
    }

    inline uint32_t trapErrorReturnOrd(const std::function<StringOrd()> &fn) noexcept {
        return trapErrorReturn<uint32_t>([&fn]() { return fn().toOrd(); });
    }

    template<typename T>
    inline T callApiReturnHandle(const std::function<uint32_t()> &fn) {
        static_assert(std::is_base_of_v<ObjHandle, T>);
        return T(callApiReturn<uint32_t>(fn));
    }

    inline StringOrd callApiReturnOrd(const std::function<uint32_t()> &fn) {
        return StringOrd(callApiReturn<uint32_t>(fn));
    }

} // namespace ggapi
