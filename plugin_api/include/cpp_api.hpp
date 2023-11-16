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

    class Symbol;
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

    typedef std::function<Struct(Scope, Symbol, Struct)> topicCallbackLambda;
    typedef std::function<void(Scope, Symbol, Struct)> lifecycleCallbackLambda;
    typedef Struct (*topicCallback_t)(Task, Symbol, Struct);
    typedef void (*lifecycleCallback_t)(ModuleScope, Symbol, Struct);
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
    uint32_t trapErrorReturnOrd(const std::function<Symbol()> &fn) noexcept;
    template<typename T>
    T callApiReturn(const std::function<T()> &fn);
    template<typename T>
    T callApiReturnHandle(const std::function<uint32_t()> &fn);
    Symbol callApiReturnOrd(const std::function<uint32_t()> &fn);
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

    /**
     * Wraps a string ordinal as consumer of the APIs
     *
     * The constructors will typically be used before a module is fully initialized
     * ggapiGetStringOrdinal is expected to only fail if out of memory, and we'll
     * consider that unrecoverable
     */
    class Symbol {
    private:
        uint32_t _asInt{0};

    public:
        static uint32_t intern(std::string_view sv) noexcept {
            uint32_t r = ::ggapiGetStringOrdinal(sv.data(), sv.length());
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

        [[nodiscard]] constexpr uint32_t asInt() const noexcept {
            return _asInt;
        }

        [[nodiscard]] std::string toString() const {
            auto len =
                callApiReturn<size_t>([*this]() { return ::ggapiGetOrdinalStringLen(_asInt); });
            return stringFillHelper(len, [*this](auto buf, auto bufLen) {
                return callApiReturn<size_t>([*this, &buf, bufLen]() {
                    return ::ggapiGetOrdinalString(_asInt, buf, bufLen);
                });
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
            Symbol topic, Struct message, topicCallback_t result, int32_t timeout = -1);

        //
        // Create a synchronous LPC call - a task handle is created, and observable by subscribers
        // however the task is deleted by the time the call returns. Most handlers are called in
        // the same (callers) thread, however this must not be assumed.
        //
        [[nodiscard]] static Struct sendToTopic(Symbol topic, Struct message, int32_t timeout = -1);

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
        [[nodiscard]] Subscription subscribeToTopic(Symbol topic, topicCallback_t callback);

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
            Symbol componentName, lifecycleCallback_t callback);
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
    public:
        using ArgValueBase =
            std::variant<bool, uint64_t, double, std::string_view, ObjHandle, Symbol>;

    protected:
        template<typename FT, typename VT>
        static decltype(auto) visitArg(FT &&fn, VT x) {
            if constexpr(std::is_same_v<bool, VT>) {
                return fn(x);
            } else if constexpr(std::is_integral_v<VT>) {
                return fn(static_cast<uint64_t>(x));
            } else if constexpr(std::is_floating_point_v<VT>) {
                return fn(static_cast<double>(x));
            } else if constexpr(std::is_assignable_v<Symbol, VT>) {
                return fn(static_cast<Symbol>(x));
            } else if constexpr(std::is_assignable_v<ObjHandle, VT>) {
                return fn(static_cast<ObjHandle>(x));
            } else if constexpr(std ::is_assignable_v<std::string_view, VT>) {
                return fn(static_cast<std::string_view>(x));
            } else if constexpr(std ::is_assignable_v<ArgValueBase, VT>) {
                return std::visit(fn, static_cast<ArgValueBase>(x));
            } else {
                static_assert(VT::usingUnsupportedType, "Unsupported type");
            }
        }

    public:
        /**
         * Variant-class for argument values. Note that this wraps the variant
         * ArgValueBase to allow control of type conversions
         */
        struct ArgValue : public ArgValueBase {
            ArgValue() = default;
            ArgValue(const ArgValue &) = default;
            ArgValue(ArgValue &&) = default;
            ArgValue &operator=(const ArgValue &) = default;
            ArgValue &operator=(ArgValue &&) = default;
            ~ArgValue() = default;

            ArgValueBase &base() {
                return *this;
            }

            [[nodiscard]] const ArgValueBase &base() const {
                return *this;
            }

            template<typename T>
            // NOLINTNEXTLINE(*-explicit-constructor)
            ArgValue(T x) : ArgValueBase(convert(x)) {
            }

            template<typename T>
            ArgValue &operator=(T x) {
                ArgValueBase::operator=(convert(x));
                return *this;
            }

            template<typename T>
            static ArgValueBase convert(T x) noexcept {
                if constexpr(std::is_same_v<bool, T>) {
                    return ArgValueBase(x);
                } else if constexpr(std::is_integral_v<T>) {
                    return static_cast<uint64_t>(x);
                } else if constexpr(std::is_floating_point_v<T>) {
                    return static_cast<double>(x);
                } else if constexpr(std::is_assignable_v<Symbol, T>) {
                    return static_cast<Symbol>(x);
                } else if constexpr(std::is_assignable_v<ObjHandle, T>) {
                    return static_cast<ObjHandle>(x);
                } else if constexpr(std ::is_assignable_v<std::string_view, T>) {
                    return static_cast<std::string_view>(x);
                } else {
                    return x;
                }
            }
        };

        using KeyValue = std::pair<Symbol, ArgValue>;

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
        void putImpl(Symbol key, bool v) {
            callApi([*this, key, v]() { ::ggapiStructPutBool(_handle, key.asInt(), v); });
        }
        void putImpl(Symbol key, uint64_t v) {
            callApi([*this, key, v]() { ::ggapiStructPutInt64(_handle, key.asInt(), v); });
        }
        void putImpl(Symbol key, double v) {
            callApi([*this, key, v]() { ::ggapiStructPutFloat64(_handle, key.asInt(), v); });
        }
        void putImpl(Symbol key, Symbol v) {
            callApi(
                [*this, key, v]() { ::ggapiStructPutStringOrd(_handle, key.asInt(), v.asInt()); });
        }
        void putImpl(Symbol key, std::string_view v) {
            callApi([*this, key, v]() {
                ::ggapiStructPutString(_handle, key.asInt(), v.data(), v.length());
            });
        }
        void putImpl(Symbol key, ObjHandle v) {
            callApi([*this, key, v]() {
                ::ggapiStructPutHandle(_handle, key.asInt(), v.getHandleId());
            });
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
        Struct &put(Symbol key, T v) {
            required();
            visitArg([this, key](auto &&v) { this->putImpl(key, v); }, v);
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

        [[nodiscard]] bool hasKey(Symbol key) const {
            required();
            return callApiReturn<bool>(
                [*this, key]() { return ::ggapiStructHasKey(_handle, key.asInt()); });
        }

        template<typename T>
        T get(Symbol key) {
            required();
            if constexpr(std::is_same_v<bool, T>) {
                return callApiReturn<bool>(
                    [*this, key]() { return ::ggapiStructGetBool(_handle, key.asInt()); });
            } else if constexpr(std::is_integral_v<T>) {
                auto intv = callApiReturn<uint64_t>(
                    [*this, key]() { return ::ggapiStructGetInt64(_handle, key.asInt()); });
                return static_cast<T>(intv);
            } else if constexpr(std::is_floating_point_v<T>) {
                auto floatv = callApiReturn<double>(
                    [*this, key]() { return ::ggapiStructGetFloat64(_handle, key.asInt()); });
                return static_cast<T>(floatv);
            } else if constexpr(std::is_assignable_v<ObjHandle, T>) {
                return callApiReturnHandle<T>(
                    [*this, key]() { return ::ggapiStructGetHandle(_handle, key.asInt()); });
            } else if constexpr(std ::is_assignable_v<std::string, T>) {
                size_t len = callApiReturn<size_t>(
                    [*this, key]() { return ::ggapiStructGetStringLen(_handle, key.asInt()); });
                return stringFillHelper(len, [*this, key](auto buf, auto bufLen) {
                    return callApiReturn<size_t>([*this, key, &buf, bufLen]() {
                        return ::ggapiStructGetString(_handle, key.asInt(), buf, bufLen);
                    });
                });
            } else {
                static_assert(T::usingUnsupportedType, "Unsupported type");
            }
        }

        template<typename T>
        T getValue(const std::initializer_list<std::string_view> &keys) {
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
        void putImpl(int32_t idx, bool v) {
            callApi([*this, idx, v]() { ::ggapiListPutBool(_handle, idx, v); });
        }
        void putImpl(int32_t idx, uint64_t v) {
            callApi([*this, idx, v]() { ::ggapiListPutInt64(_handle, idx, v); });
        }
        void putImpl(int32_t idx, double v) {
            callApi([*this, idx, v]() { ::ggapiListPutFloat64(_handle, idx, v); });
        }
        void putImpl(int32_t idx, Symbol v) {
            callApi([*this, idx, v]() { ::ggapiListPutStringOrd(_handle, idx, v.asInt()); });
        }
        void putImpl(int32_t idx, std::string_view v) {
            callApi(
                [*this, idx, v]() { ::ggapiListPutString(_handle, idx, v.data(), v.length()); });
        }
        void putImpl(int32_t idx, ObjHandle v) {
            callApi([*this, idx, v]() { ::ggapiListPutHandle(_handle, idx, v.getHandleId()); });
        }
        void insertImpl(int32_t idx, bool v) {
            callApi([*this, idx, v]() { ::ggapiListInsertBool(_handle, idx, v); });
        }
        void insertImpl(int32_t idx, uint64_t v) {
            callApi([*this, idx, v]() { ::ggapiListInsertInt64(_handle, idx, v); });
        }
        void insertImpl(int32_t idx, double v) {
            callApi([*this, idx, v]() { ::ggapiListInsertFloat64(_handle, idx, v); });
        }
        void insertImpl(int32_t idx, Symbol v) {
            callApi([*this, idx, v]() { ::ggapiListInsertStringOrd(_handle, idx, v.asInt()); });
        }
        void insertImpl(int32_t idx, std::string_view v) {
            callApi(
                [*this, idx, v]() { ::ggapiListInsertString(_handle, idx, v.data(), v.length()); });
        }
        void insertImpl(int32_t idx, ObjHandle v) {
            callApi([*this, idx, v]() { ::ggapiListInsertHandle(_handle, idx, v.getHandleId()); });
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
            visitArg([this, idx](auto &&v) { this->putImpl(idx, v); }, v);
            return *this;
        }

        template<typename T>
        List &insert(int32_t idx, T v) {
            required();
            visitArg([this, idx](auto &&v) { this->insertImpl(idx, v); }, v);
            return *this;
        }

        List &append(const ArgValue &value) {
            required();
            std::visit([this](auto &&value) { insert(-1, value); }, value.base());
            return *this;
        }

        List &append(std::initializer_list<ArgValue> list) {
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
            } else if constexpr(std::is_assignable_v<ObjHandle, T>) {
                return callApiReturnHandle<T>(
                    [*this, idx]() { return ::ggapiListGetHandle(_handle, idx); });
            } else if constexpr(std ::is_assignable_v<std::string, T>) {
                size_t len = callApiReturn<size_t>(
                    [*this, idx]() { return ::ggapiListGetStringLen(_handle, idx); });
                return stringFillHelper(len, [*this, idx](auto buf, auto bufLen) {
                    return callApiReturn<size_t>([*this, idx, &buf, bufLen]() {
                        return ::ggapiListGetString(_handle, idx, buf, bufLen);
                    });
                });
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

    inline Subscription Scope::subscribeToTopic(Symbol topic, topicCallback_t callback) {
        required();
        return callApiReturnHandle<Subscription>([*this, topic, callback]() {
            return ::ggapiSubscribeToTopic(
                getHandleId(),
                topic.asInt(),
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
        Symbol topic, Struct message, topicCallback_t result, int32_t timeout) {
        return callApiReturnHandle<Task>([topic, message, result, timeout]() {
            return ::ggapiSendToTopicAsync(
                topic.asInt(),
                message.getHandleId(),
                topicCallbackProxy,
                reinterpret_cast<uintptr_t>(result), // NOLINT(*-reinterpret-cast)
                timeout);
        });
    }

    inline Struct Task::sendToTopic(ggapi::Symbol topic, Struct message, int32_t timeout) {
        return callApiReturnHandle<Struct>([topic, message, timeout]() {
            return ::ggapiSendToTopic(topic.asInt(), message.getHandleId(), timeout);
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
        Symbol componentName, lifecycleCallback_t callback) {
        required();
        return callApiReturnHandle<ModuleScope>([*this, componentName, callback]() {
            return ::ggapiRegisterPlugin(
                getHandleId(),
                componentName.asInt(),
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
            return callback(Task{taskHandle}, Symbol{topicOrd}, Struct{dataStruct}).getHandleId();
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
            callback(ModuleScope{moduleHandle}, Symbol{phaseOrd}, Struct{dataStruct});
            return true;
        });
    }

    //
    // Symbol constants - done as inline methods so that only used constants are included. Note
    // that ordinal gets are idempotent and thread safe.
    //
    struct Consts {

        static Symbol error() {
            static const Symbol error{"error"};
            return error;
        }
    };

    //
    // Translated exception
    //
    class GgApiError : public std::exception {
        Symbol _symbol;

    public:
        constexpr GgApiError(const GgApiError &) noexcept = default;
        constexpr GgApiError(GgApiError &&) noexcept = default;
        GgApiError &operator=(const GgApiError &) noexcept = default;
        GgApiError &operator=(GgApiError &&) noexcept = default;

        explicit GgApiError(const Symbol &errorClass) noexcept : _symbol{errorClass} {
        }

        explicit GgApiError(std::string_view errorClass) noexcept : _symbol{errorClass} {
        }

        ~GgApiError() override = default;

        constexpr explicit operator Symbol() const {
            return _symbol;
        }

        [[nodiscard]] constexpr Symbol get() const {
            return _symbol;
        }
    };

    //
    // Helper function to throw an exception if there is a thread error
    //
    inline void rethrowOnThreadError() {
        uint32_t errCode = ggapiGetError();
        if(errCode != 0) {
            ggapiSetError(0); // consider error 'handled'
            throw GgApiError(Symbol(errCode));
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
            ggapiSetError(Consts::error().asInt());
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

    inline uint32_t trapErrorReturnOrd(const std::function<Symbol()> &fn) noexcept {
        return trapErrorReturn<uint32_t>([&fn]() { return fn().asInt(); });
    }

    template<typename T>
    inline T callApiReturnHandle(const std::function<uint32_t()> &fn) {
        static_assert(std::is_base_of_v<ObjHandle, T>);
        return T(callApiReturn<uint32_t>(fn));
    }

    inline Symbol callApiReturnOrd(const std::function<uint32_t()> &fn) {
        return Symbol(callApiReturn<uint32_t>(fn));
    }

} // namespace ggapi
