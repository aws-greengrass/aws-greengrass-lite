#pragma once

#include "buffer_stream.hpp"
#include "util.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "c_api.hpp"

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
    class TopicCallback;
    class TaskCallback;
    class LifecycleCallback;
    class ChannelListenCallback;
    class ChannelCloseCallback;
    class CallbackManager;
    class GgApiError;

    using TopicCallbackLambda = std::function<Struct(Task, Symbol, Struct)>;
    using LifecycleCallbackLambda = std::function<bool(ModuleScope, Symbol, Struct)>;
    using TaskCallbackLambda = std::function<void(Struct)>;

    template<typename T>
    T trapErrorReturn(const std::function<T()> &fn) noexcept;
    template<typename T>
    T callApiReturn(const std::function<T()> &fn);
    template<typename T>
    T callApiReturnHandle(const std::function<uint32_t()> &fn);
    void callApi(const std::function<void()> &fn);

    // Helper functions for consistent string copy pattern
    template<
        class Traits = std::char_traits<char>,
        class Alloc = std::allocator<char>,
        class StringFillFn>
    std::basic_string<char, Traits, Alloc> stringFillHelper(
        size_t strLen, StringFillFn stringFillFn) {
        static_assert(std::is_invocable_v<StringFillFn, char *, size_t>);
        static_assert(
            std::is_convertible_v<std::invoke_result_t<StringFillFn, char *, size_t>, size_t>);
        if(strLen == 0) {
            return {};
        }
        std::basic_string<char, Traits, Alloc> str(strLen, '\0');
        size_t finalLen = stringFillFn(str.data(), str.size());
        str.resize(finalLen);
        return str;
    }

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

        [[nodiscard]] constexpr uint32_t asInt() const noexcept {
            return _asInt;
        }

        [[nodiscard]] std::string toString() const {
            auto len =
                callApiReturn<size_t>([*this]() { return ::ggapiGetSymbolStringLen(_asInt); });
            return stringFillHelper(len, [*this](auto buf, auto bufLen) {
                return callApiReturn<size_t>([*this, &buf, bufLen]() {
                    return ::ggapiGetSymbolString(_asInt, buf, bufLen);
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

    /**
     * A task handle represents an active LPC operation or deferred function call. The handle is
     * deleted after the completion callback (if any).
     */
    class Task : public ObjHandle {
        void check() {
            if(getHandleId() != 0 && !isTask()) {
                throw std::runtime_error("Task handle expected");
            }
        }

    public:
        constexpr Task() noexcept = default;

        explicit Task(const ObjHandle &other) : ObjHandle{other} {
            check();
        }

        explicit Task(uint32_t handle) : ObjHandle{handle} {
            check();
        }

        /**
         * Changes affinitized callback model. Listeners created in this thread will only
         * be executed in same thread. Tasks created in this thread will use this thread by
         * default for callbacks if not otherwise affinitized. See individual functions
         * for single thread behavior.
         */
        static void setSingleThread(bool singleThread) {
            callApi([singleThread]() { ::ggapiSetSingleThread(singleThread); });
        }

        /**
         * Create an asynchronous LPC call - returning the Task handle for the call. This
         * function allows a "run later" behavior (e.g. for retries). If calling thread is
         * marked as "single thread", any callbacks not already affinitized will run on this
         * thread during "waitForTaskCompleted".
         */
        [[nodiscard]] static Task sendToTopicAsync(
            Symbol topic,
            Struct message,
            const TopicCallbackLambda &resultCallback,
            int32_t timeout = -1);

        /**
         * Generic form of sendToTopicAsync
         */
        [[nodiscard]] static Task sendToTopicAsync(
            Symbol topic, Struct message, TopicCallback resultCallback, int32_t timeout = -1);

        /**
         * Create a synchronous LPC call - a task handle is created, and observable by subscribers
         * however the task is deleted by the time the call returns. Most handlers are called in
         * the same (callers) thread as if "setSingleThread" set to true, however this must not be
         * assumed as some callbacks may be affinitized to another thread.
         */
        [[nodiscard]] static Struct sendToTopic(Symbol topic, Struct message, int32_t timeout = -1);

        /**
         * A deferred asynchronous call using the task system. If calling thread is in "single
         * thread" mode, the call will not run until "waitForTaskCompleted" is called (for any
         * task).
         */
        [[nodiscard]] static Task callAsync(
            Struct data, const TaskCallbackLambda &callback, uint32_t delay = 0);

        /**
         * Generic form of callAsync
         */
        [[nodiscard]] static Task callAsync(Struct data, TaskCallback callback, uint32_t delay = 0);

        /**
         * Block until task completes including final callback if there is one. If thread is in
         * "single thread" mode, callbacks will execute during this callback even if associated
         * with other tasks.
         */
        [[nodiscard]] Struct waitForTaskCompleted(int32_t timeout = -1);

        /**
         * Block for set period of time while allowing thread to be used for other tasks.
         */
        static void sleep(uint32_t duration);

        /**
         * Cancel task - if a callback is asynchronously executing, they will still continue to
         * run, it does not kill underlying threads.
         */
        void cancelTask();

        /**
         * When in a task callback, returns the associated task. When not in a task callback, it
         * returns a task handle associated with the thread.
         */
        [[nodiscard]] static Task current();
    };

    /**
     * Subscription handles indicate an active listener for LPC topics. Anonymous listeners
     * can also exist. Subscriptions are associated with a scope. A module-scope subscription
     * will exist for the entire lifetime of the module. A local-scope subscription will exist
     * until the enclosing scope returns (useful for single-thread subscriptions).
     */
    class Subscription : public ObjHandle {
        void check() {
            if(getHandleId() != 0 && !isSubscription()) {
                throw std::runtime_error("Subscription handle expected");
            }
        }

    public:
        constexpr Subscription() noexcept = default;

        explicit Subscription(const ObjHandle &other) : ObjHandle{other} {
            check();
        }

        explicit Subscription(uint32_t handle) : ObjHandle{handle} {
            check();
        }

        /**
         * Send a message to this specific subscription. Return immediately. If the calling thread
         * is in "single thread" mode, the 'result' callback will not execute until
         * waitForTaskCompleted is called in the same thread.
         */
        [[nodiscard]] Task callAsync(
            Struct message, const TopicCallbackLambda &resultCallback, int32_t timeout = -1) const;

        /**
         * Generic form of callAsync
         */
        [[nodiscard]] Task callAsync(
            Struct message, TopicCallback result, int32_t timeout = -1) const;

        /**
         * Send a message to this specific subscription. Wait until task completes, as if
         * waitForTaskCompleted is called on the same thread.
         */
        [[nodiscard]] Struct call(Struct message, int32_t timeout = -1) const;
    };

    /**
     * Scopes are a class of handles that are used as targets for anchoring other handles.
     * See the subclasses to understand the specific types of scopes. There are currently two
     * kinds of scopes - Module scope (for the duration plugin is loaded), and Call scope
     * (stack-based).
     */
    class Scope : public ObjHandle {

        void check() {
            if(getHandleId() != 0 && !isScope()) {
                throw std::runtime_error("Scope handle expected");
            }
        }

    public:
        constexpr Scope() noexcept = default;
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
        [[nodiscard]] Subscription subscribeToTopic(
            Symbol topic, const TopicCallbackLambda &callback);

        //
        // Generic form of subscribeToTopic
        //
        [[nodiscard]] Subscription subscribeToTopic(Symbol topic, TopicCallback callback);

        //
        // Anchor an object against this scope.
        //
        template<typename T>
        [[nodiscard]] T anchor(T otherHandle) const;
    };

    /**
     * Module scope. For module-global data. Typically used for listeners.
     */
    class ModuleScope : public Scope {
    public:
        constexpr ModuleScope() noexcept = default;
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
            Symbol componentName, const LifecycleCallbackLambda &callback);

        [[nodiscard]] ModuleScope registerPlugin(Symbol componentName, LifecycleCallback callback);

        // Note: Using this can lead to unpredictable results if the parent plugin is unloaded.
        [[nodiscard]] static ModuleScope registerGlobalPlugin(
            Symbol componentName, const LifecycleCallbackLambda &callback);

        [[nodiscard]] static ModuleScope registerGlobalPlugin(
            Symbol componentName, LifecycleCallback callback);

        ModuleScope setActive() {
            return callApiReturnHandle<ModuleScope>(
                [this]() { return ::ggapiChangeModule(getHandleId()); });
        }

        [[nodiscard]] static ModuleScope current() {
            return callApiReturnHandle<ModuleScope>([]() { return ::ggapiGetCurrentModule(); });
        }
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

    /**
     * Containers are the root for Structures, Lists and Buffers.
     */
    class Container : public ObjHandle {
    public:
        using ArgValueBase =
            std::variant<bool, uint64_t, double, std::string_view, ObjHandle, Symbol>;

    protected:
        template<typename Visitor, typename T>
        static auto visitArg(Visitor &&fn, const T &x) {
            if constexpr(std::is_same_v<bool, T>) {
                return fn(x);
            } else if constexpr(std::is_integral_v<T>) {
                return fn(static_cast<uint64_t>(x));
            } else if constexpr(std::is_floating_point_v<T>) {
                return fn(static_cast<double>(x));
            } else if constexpr(std::is_base_of_v<Symbol, T>) {
                return fn(static_cast<Symbol>(x));
            } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
                return fn(static_cast<ObjHandle>(x));
            } else if constexpr(std ::is_assignable_v<std::string_view, T>) {
                return fn(static_cast<std::string_view>(x));
            } else if constexpr(std ::is_assignable_v<ArgValueBase, T>) {
                return std::visit(std::forward<Visitor>(fn), static_cast<ArgValueBase>(x));
            } else {
                static_assert(T::usingUnsupportedType, "Unsupported type");
            }
        }

    private:
        static Container boxImpl(bool v) {
            return callApiReturnHandle<Container>([v]() { return ::ggapiBoxBool(v); });
        }
        static Container boxImpl(uint64_t v) {
            return callApiReturnHandle<Container>([v]() { return ::ggapiBoxInt64(v); });
        }
        static Container boxImpl(double v) {
            return callApiReturnHandle<Container>([v]() { return ::ggapiBoxFloat64(v); });
        }
        static Container boxImpl(std::string_view v) {
            return callApiReturnHandle<Container>(
                [v]() { return ::ggapiBoxString(v.data(), v.length()); });
        }
        static Container boxImpl(Symbol v) {
            return callApiReturnHandle<Container>([v]() { return ::ggapiBoxSymbol(v.asInt()); });
        }
        static Container boxImpl(ObjHandle v) {
            return callApiReturnHandle<Container>(
                [v]() { return ::ggapiBoxHandle(v.getHandleId()); });
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
            ArgValue(const T &x) : ArgValueBase(convert(x)) {
            }

            template<typename T>
            ArgValue &operator=(const T &x) {
                ArgValueBase::operator=(convert(x));
                return *this;
            }

            template<typename T>
            static ArgValueBase convert(const T &x) noexcept {
                if constexpr(std::is_same_v<bool, T>) {
                    return ArgValueBase(x);
                } else if constexpr(std::is_integral_v<T>) {
                    return static_cast<uint64_t>(x);
                } else if constexpr(std::is_floating_point_v<T>) {
                    return static_cast<double>(x);
                } else if constexpr(std::is_base_of_v<Symbol, T>) {
                    return static_cast<Symbol>(x);
                } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
                    return static_cast<ObjHandle>(x);
                } else if constexpr(std ::is_assignable_v<std::string_view, T>) {
                    return static_cast<std::string_view>(x);
                } else {
                    return x;
                }
            }
        };

        using KeyValue = std::pair<Symbol, ArgValue>;

        constexpr Container() noexcept = default;

        explicit Container(const ObjHandle &other) : ObjHandle{other} {
        }

        explicit Container(uint32_t handle) : ObjHandle{handle} {
        }

        [[nodiscard]] uint32_t size() const {
            return callApiReturn<uint32_t>([*this]() { return ::ggapiGetSize(_handle); });
        }

        [[nodiscard]] bool empty() const {
            return callApiReturn<bool>([*this]() { return ::ggapiIsEmpty(_handle); });
        }

        /**
         * Create a buffer that represents the JSON string for this container. If the container
         * is a buffer, it is treated as a string.
         */
        [[nodiscard]] Buffer toJson() const;
        /**
         * Create a buffer that represents the YAML string for this container. If the container
         * is a buffer, it is treated as a string.
         */
        [[nodiscard]] Buffer toYaml() const;

        /**
         * Convert a scalar value to a boxed container.
         */
        template<typename T>
        [[nodiscard]] static Container box(T v) {
            return visitArg([](auto &&v) { boxImpl(v); }, v);
        }

        /**
         * Convert boxed container type into unboxed type, throwing an exception
         * if conversion cannot be performed.
         *
         * @tparam T unboxed type
         * @return value converted to requested type
         */
        template<typename T>
        [[nodiscard]] T unbox() const {
            required();
            if constexpr(std::is_same_v<bool, T>) {
                return callApiReturn<bool>([*this]() { return ::ggapiUnboxBool(_handle); });
            } else if constexpr(std::is_integral_v<T>) {
                auto intv =
                    callApiReturn<uint64_t>([*this]() { return ::ggapiUnboxInt64(_handle); });
                return static_cast<T>(intv);
            } else if constexpr(std::is_floating_point_v<T>) {
                auto floatv =
                    callApiReturn<double>([*this]() { return ::ggapiUnboxFloat64(_handle); });
                return static_cast<T>(floatv);
            } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
                return callApiReturnHandle<T>([*this]() { return ::ggapiUnboxHandle(_handle); });
            } else if constexpr(std ::is_assignable_v<std::string, T>) {
                size_t len =
                    callApiReturn<size_t>([*this]() { return ::ggapiUnboxStringLen(_handle); });
                return stringFillHelper(len, [*this](auto buf, auto bufLen) {
                    return callApiReturn<size_t>([*this, &buf, bufLen]() {
                        return ::ggapiUnboxString(_handle, buf, bufLen);
                    });
                });
            } else {
                static_assert(T::usingUnsupportedType, "Unsupported type");
            }
        }
    };

    /**
     * Channels are streams of data.
     */
    class Channel : public ObjHandle {
        void check() {
            if(getHandleId() != 0 && !isChannel()) {
                throw std::runtime_error("Subscription handle expected");
            }
        }

    public:
        constexpr Channel() noexcept = default;

        explicit Channel(const ObjHandle &other) : ObjHandle{other} {
            check();
        }

        explicit Channel(uint32_t handle) : ObjHandle{handle} {
            check();
        }

        static Channel create() {
            return Channel(::ggapiCreateChannel());
        }

        void write(ObjHandle v) const {
            ::ggapiChannelWrite(_handle, v.getHandleId());
        }

        void close() const {
            ::ggapiChannelClose(_handle);
        }

        inline void addListenCallback(ChannelListenCallback callback);
        template<typename Callback, typename... Args>
        inline void addListenCallback(const Callback callback, Args &&...args);

        inline void addCloseCallback(ChannelCloseCallback callback);
        template<typename Callback, typename... Args>
        inline void addCloseCallback(const Callback callback, Args &&...args);
    };

    /**
     * Structures are containers with associative keys.
     */
    class Struct : public Container {
        void check() {
            if(getHandleId() != 0 && !isStruct()) {
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
            callApi([*this, key, v]() { ::ggapiStructPutSymbol(_handle, key.asInt(), v.asInt()); });
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
        constexpr Struct() noexcept = default;

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
        [[nodiscard]] T get(Symbol key) const {
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
            } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
                return callApiReturnHandle<T>(
                    [*this, key]() { return ::ggapiStructGetHandle(_handle, key.asInt()); });
            } else if constexpr(std ::is_assignable_v<std::string, T>) {
                size_t len = callApiReturn<size_t>(
                    [*this, key]() { return ::ggapiStructGetStringLen(_handle, key.asInt()); });
                return stringFillHelper<typename T::traits_type, typename T::allocator_type>(
                    len, [*this, key](auto buf, auto bufLen) {
                        return callApiReturn<size_t>([*this, key, &buf, bufLen]() {
                            return ::ggapiStructGetString(_handle, key.asInt(), buf, bufLen);
                        });
                    });
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

    /**
     * Lists are containers with index-based keys
     */
    class List : public Container {
        void check() {
            if(getHandleId() != 0 && !isList()) {
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
            callApi([*this, idx, v]() { ::ggapiListPutSymbol(_handle, idx, v.asInt()); });
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
            callApi([*this, idx, v]() { ::ggapiListInsertSymbol(_handle, idx, v.asInt()); });
        }
        void insertImpl(int32_t idx, std::string_view v) {
            callApi(
                [*this, idx, v]() { ::ggapiListInsertString(_handle, idx, v.data(), v.length()); });
        }
        void insertImpl(int32_t idx, ObjHandle v) {
            callApi([*this, idx, v]() { ::ggapiListInsertHandle(_handle, idx, v.getHandleId()); });
        }

    public:
        constexpr List() noexcept = default;

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
        [[nodiscard]] T get(int32_t idx) {
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
            } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
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

    class Buffer;
    using BufferStream = util::BufferStreamBase<Buffer>;
    using BufferInStream = util::BufferInStreamBase<BufferStream>;
    using BufferOutStream = util::BufferOutStreamBase<BufferStream>;

    /**
     * Buffers are shared mutable containers of bytes.
     */
    class Buffer : public Container {
        void check() {
            if(getHandleId() != 0 && !isBuffer()) {
                throw std::runtime_error("Buffer handle expected");
            }
        }

    public:
        constexpr Buffer() noexcept = default;

        explicit Buffer(const ObjHandle &other) : Container{other} {
            check();
        }

        explicit Buffer(uint32_t handle) : Container{handle} {
            check();
        }

        static Buffer create() {
            return Buffer(::ggapiCreateBuffer());
        }

        /**
         * BufferStream should be used only on buffers that will not be read/modified by a different
         * thread until closed.
         */
        BufferStream stream();
        /**
         * BufferInStream should be used only on buffers that will not be read/modified by a
         * different thread until closed.
         */
        BufferInStream in();
        /**
         * BufferOutStream should be used only on buffers that will not be read/modified by a
         * different thread until closed.
         */
        BufferOutStream out();

        template<typename DataT, typename SizeT>
        Buffer &put(int32_t idx, util::Span<DataT, SizeT> span) {
            return put(idx, util::as_bytes(span));
        }

        template<typename SizeT>
        Buffer &put(int32_t idx, util::Span<const char, SizeT> bytes) {
            if(bytes.size() > std::numeric_limits<uint32_t>::max()) {
                throw std::out_of_range("length out of range");
            }
            required();
            callApi([*this, idx, bytes]() {
                ::ggapiBufferPut(_handle, idx, bytes.data(), bytes.size());
            });
            return *this;
        }

        template<typename T, class Alloc>
        Buffer &put(int32_t idx, const std::vector<T, Alloc> &vec) {
            return put(idx, util::as_bytes(util::Span{vec}));
        }

        template<typename CharT, class Traits>
        Buffer &put(int32_t idx, std::basic_string_view<CharT, Traits> sv) {
            return put(idx, util::as_bytes(util::Span<const CharT>{sv}));
        }

        template<typename DataT, typename SizeT>
        Buffer &insert(int32_t idx, util::Span<DataT, SizeT> span) {
            return insert(idx, util::as_bytes(span));
        }

        template<typename SizeT>
        Buffer &insert(int32_t idx, util::Span<const char, SizeT> bytes) {
            if(bytes.size() > std::numeric_limits<uint32_t>::max()) {
                throw std::out_of_range("length out of range");
            }
            required();
            callApi([*this, idx, bytes]() {
                ::ggapiBufferInsert(_handle, idx, bytes.data(), bytes.size());
            });
            return *this;
        }

        template<typename T, class Alloc>
        Buffer &insert(int32_t idx, const std::vector<T, Alloc> &vec) {
            return insert(idx, util::as_bytes(util::Span{vec}));
        }

        template<typename CharT, class Traits>
        Buffer &insert(int32_t idx, const std::basic_string_view<CharT, Traits> sv) {
            return insert(idx, util::as_bytes(util::Span{sv}));
        }

        template<typename DataT, typename SizeT>
        uint32_t get(int32_t idx, util::Span<DataT, SizeT> span) const {
            return get(idx, util::as_writeable_bytes(span)) / sizeof(DataT);
        }

        template<typename SizeT>
        uint32_t get(int32_t idx, util::Span<char, SizeT> bytes) const {
            if(bytes.size() > std::numeric_limits<uint32_t>::max()) {
                throw std::out_of_range("length out of range");
            }

            required();
            return callApiReturn<uint32_t>([this, idx, bytes]() -> uint32_t {
                return ::ggapiBufferGet(_handle, idx, bytes.data(), bytes.size());
            });
        }

        template<typename T, class Alloc>
        size_t get(int32_t idx, std::vector<T, Alloc> &vec) const {
            size_t actual = get(idx, util::as_writeable_bytes(util::Span{vec})) / sizeof(T);
            vec.resize(actual);
            return actual;
        }

        template<typename CharT, class Traits, class Alloc>
        size_t get(int32_t idx, std::basic_string<CharT, Traits, Alloc> &str) const {
            size_t actual = get(idx, util::as_writeable_bytes(util::Span{str})) / sizeof(CharT);
            str.resize(actual);
            return actual;
        }

        template<typename T>
        T get(int32_t idx, size_t max) const {
            if(max > std::numeric_limits<uint32_t>::max()) {
                throw std::out_of_range("max length out of range");
            }
            T buffer;
            buffer.resize(std::min<size_t>(size(), max));
            get(idx, buffer);
            return buffer;
        }

        Buffer &resize(uint32_t newSize) {
            required();
            callApi([*this, newSize]() { ::ggapiBufferResize(_handle, newSize); });
            return *this;
        }

        /**
         * Parse buffer as if a JSON string. Type of container depends on type of JSON structure.
         */
        [[nodiscard]] Container fromJson() const {
            required();
            return callApiReturnHandle<Container>([*this]() { return ::ggapiFromJson(_handle); });
        }

        /**
         * Parse buffer as if a YAML string. Type of container depends on type of YAML structure.
         */
        [[nodiscard]] Container fromYaml() const {
            required();
            return callApiReturnHandle<Container>([*this]() { return ::ggapiFromYaml(_handle); });
        }
    };

    inline BufferStream Buffer::stream() {
        return BufferStream(*this);
    }

    inline BufferInStream Buffer::in() {
        return BufferInStream(std::move(stream()));
    }

    inline BufferOutStream Buffer::out() {
        return BufferOutStream(std::move(stream()));
    }

    template<typename T>
    inline T Scope::anchor(T otherHandle) const {
        required();
        static_assert(std::is_base_of_v<ObjHandle, T>);
        return callApiReturnHandle<T>([this, otherHandle]() {
            return ::ggapiAnchorHandle(getHandleId(), otherHandle.getHandleId());
        });
    }

    /**
     * Factory to serve out callback handles allowing rich C++ style callbacks while maintaining
     * a C interface to the API. Note that while this supports lambdas, lambdas should be used
     * with caution. Better to use a callback function and callback parameters (see std::thread).
     */
    class CallbackManager {

    public:
        using Delegate = std::function<uint32_t()>;

        /**
         * Base class for dispatch classes. Subclasses need to implement prepare to construct
         * a delegate lambda that will invoke the callback implementation.
         */
        struct CallbackDispatch {
            /**
             * Implement to creates a new lambda that wraps saved callback, ready to be called. This
             * operation occurs inside a lock so the new lambda is used after releasing the lock.
             *
             * @param callbackType The 'type' of callback for validation
             * @param size Size of callback structure for validation
             * @param data Anonymous pointer to callback structure
             * @return Delegate lambda ready for calling
             */
            [[nodiscard]] virtual Delegate prepare(
                uint32_t callbackType, uint32_t size, const void *data) const = 0;
            /**
             * Expected callback type for validation
             */
            [[nodiscard]] virtual Symbol type() const = 0;

            CallbackDispatch() = default;
            CallbackDispatch(const CallbackDispatch &) = default;
            CallbackDispatch(CallbackDispatch &&) = default;
            CallbackDispatch &operator=(const CallbackDispatch &) = default;
            CallbackDispatch &operator=(CallbackDispatch &&) = default;
            virtual ~CallbackDispatch() = default;

            void assertCallbackType(Symbol actual) const {
                if(actual != type()) {
                    throw std::runtime_error(
                        "Mismatch callback type - received " + actual.toString() + " instead of "
                        + type().toString());
                }
            }

            /**
             * The structure passed to the plugin from the Nucleus is anonymous. We know how
             * to interpret this structure based on (1) matching context, (2) matching type,
             * and (3) checking that the passed in structure is not too small. The passed in
             * structure can be bigger if, for example, a newer version of Nucleus adds additional
             * context, in which case, that additional context is ignored by older plugins.
             * @tparam T Expected structure type
             * @param size Size of structure reported by Nucleus
             * @param data Anonymous pointer to structure
             * @return Structure after trivial validation
             */
            template<typename T>
            static const T &checkedStruct(uint32_t size, const void *data) {
                if(data == nullptr) {
                    throw std::runtime_error("Null pointer provided to callback");
                }
                if(size < sizeof(T)) {
                    throw std::runtime_error(
                        "Structure size error - maybe running with earlier version of Nucleus");
                }
                // Note, larger structure is ok - expectation is that new fields are added to end
                // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
                return *reinterpret_cast<const T *>(data);
            }
        };

    private:
        std::shared_mutex _mutex;
        std::map<uintptr_t, std::unique_ptr<const CallbackDispatch>> _callbacks;

        /**
         * Round-trip point of entry that was passed to Nucleus for Nucleus to use when performing
         * a callback.
         *
         * @param callbackContext Round-trip context, large enough to hold a pointer
         * @param callbackType Callback type, indicating what structure was passed
         * @param callbackDataSize Size of structure for validation
         * @param callbackData Pointer to structure based on previous fields
         * @return Interpretation of return value depends on callbackType
         */
        static uint32_t _callback(
            uintptr_t callbackContext,
            uint32_t callbackType,
            uint32_t callbackDataSize,
            const void *callbackData) noexcept {

            return self().callback(callbackContext, callbackType, callbackDataSize, callbackData);
        }

        uint32_t callback(
            uintptr_t callbackContext,
            uint32_t callbackType,
            uint32_t callbackDataSize,
            const void *callbackData) {

            if(callbackType == 0) {
                // Nucleus indicates callback is no longer required
                std::unique_lock guard{_mutex};
                _callbacks.erase(callbackContext);
                return 0;
            } else {
                // An actual call
                std::shared_lock guard{_mutex};
                // We could enable a fast "unsafe" option that just casts the callbackContext
                // to a pointer. For now, this acts as a robust double-check.
                const auto &cb = _callbacks.at(callbackContext);
                // Pre-process callback while lock is held
                auto delegate = cb->prepare(callbackType, callbackDataSize, callbackData);
                guard.unlock();
                // Actual call
                return trapErrorReturn<uint32_t>(delegate);
            }
        }

        ObjHandle wrapHelper(std::unique_ptr<CallbackDispatch> cb) {
            // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
            auto idx = reinterpret_cast<uintptr_t>(cb.get());
            Symbol type = cb->type();
            std::unique_lock guard{_mutex};
            _callbacks.emplace(idx, std::move(cb));
            return callApiReturnHandle<ObjHandle>([idx, type]() {
                return ::ggapiRegisterCallback(&CallbackManager::_callback, idx, type.asInt());
            });
        }

    public:
        /**
         * Register callback with Nucleus. The handle will be used to re-reference the callback
         * for the intended function. The handle only needs local scope, as the Nucleus maintains
         * the correct scope to hold on to the callback. Note, there is no way to prevent the
         * actual callback function becoming invalid after this call. That all depends on C++
         * scoping rules.
         *
         * @param cb Callback object that is used to dispatch to actual callback
         * @return Typed Handle to registered callback
         */
        template<typename CallbackType>
        CallbackType registerWithNucleus(std::unique_ptr<CallbackDispatch> cb) {
            return CallbackType(wrapHelper(std::move(cb)));
        }

        /**
         * Singleton
         */
        static CallbackManager &self() {
            static CallbackManager singleton{};
            return singleton;
        }
    };

    /**
     * Abstract topic callback
     */
    class TopicCallback : public ObjHandle {

        /**
         * Templated dispatch allows for capturing multiple arguments to pass to a callback
         * routine. Note that this enables the same callback approach as std::invoke and std::thread
         * where a typical use is (callbackMethod, this) but acceptable also to do something like
         * (callbackMethod, this, extraArg1, extraArg2).
         * @tparam Callable Lambda, function pointer, method, etc.
         * @tparam Args Prefix arguments, particularly optional This
         */
        template<typename Callable, typename... Args>
        class TopicDispatch : public CallbackManager::CallbackDispatch {

            const Callable _callable;
            const std::tuple<Args...> _args;

        public:
            explicit TopicDispatch(Callable callable, Args &&...args)
                : _callable{std::move(callable)}, _args{std::forward<Args>(args)...} {
                static_assert(
                    std::is_invocable_r_v<Struct, Callable, Args..., Task, Symbol, Struct>);
            }
            [[nodiscard]] Symbol type() const override {
                return {"topic"};
            }
            [[nodiscard]] CallbackManager::Delegate prepare(
                uint32_t callbackType, uint32_t size, const void *data) const override {
                assertCallbackType(Symbol(callbackType));
                auto &cb = checkedStruct<TopicCallbackData>(size, data);
                auto callable = _callable; // capture copy
                auto task = Task(cb.taskHandle);
                auto topic = Symbol(cb.topicSymbol);
                auto dataStruct = Struct(cb.dataStruct);
                auto args = std::tuple_cat(_args, std::tuple{task, topic, dataStruct});
                return [callable, args]() {
                    Struct s = std::apply(callable, args);
                    return s.getHandleId();
                };
            }
        };

    public:
        constexpr TopicCallback() noexcept = default;

        explicit TopicCallback(const ObjHandle &other) : ObjHandle{other} {
        }

        /**
         * Create reference to a topic callback.
         */
        template<typename Callable, typename... Args>
        static TopicCallback of(const Callable &callable, Args &&...args) {
            auto dispatch = std::make_unique<TopicDispatch<Callable, Args...>>(
                callable, std::forward<Args>(args)...);
            return CallbackManager::self().registerWithNucleus<TopicCallback>(std::move(dispatch));
        }
    };

    /**
     * Abstract task callback
     */
    class TaskCallback : public ObjHandle {

        /**
         * Templated dispatch allows for capturing multiple arguments to pass to a callback
         * routine. Note that this enables the same callback approach as std::invoke and std::thread
         * where a typical use is (callbackMethod, this) but acceptable also to do something like
         * (callbackMethod, this, extraArg1, extraArg2).
         * @tparam Callable Lambda, function pointer, method, etc.
         * @tparam Args Prefix arguments, particularly optional This
         */
        template<typename Callable, typename... Args>
        class TaskDispatch : public CallbackManager::CallbackDispatch {

            const Callable _callable;
            const std::tuple<Args...> _args;

        public:
            explicit TaskDispatch(Callable callable, Args &&...args)
                : _callable{std::move(callable)}, _args{std::forward<Args>(args)...} {
                static_assert(std::is_invocable_v<Callable, Args..., Struct>);
            }
            [[nodiscard]] Symbol type() const override {
                return {"task"};
            }
            [[nodiscard]] CallbackManager::Delegate prepare(
                uint32_t callbackType, uint32_t size, const void *data) const override {
                assertCallbackType(Symbol(callbackType));
                auto &cb = checkedStruct<TaskCallbackData>(size, data);
                auto callable = _callable;
                auto dataStruct = Struct(cb.dataStruct);
                auto args = std::tuple_cat(_args, std::tuple{dataStruct});
                return [callable, args]() {
                    std::apply(callable, args);
                    return static_cast<uint32_t>(true);
                };
            }
        };

    public:
        constexpr TaskCallback() noexcept = default;

        explicit TaskCallback(const ObjHandle &other) : ObjHandle{other} {
        }

        /**
         * Create reference to a simple async task callback.
         */
        template<typename Callable, typename... Args>
        static TaskCallback of(const Callable &callable, Args &&...args) {
            auto dispatch = std::make_unique<TaskDispatch<Callable, Args...>>(
                callable, std::forward<Args>(args)...);
            return CallbackManager::self().registerWithNucleus<TaskCallback>(std::move(dispatch));
        }
    };

    /**
     * Abstract lifecycle callback
     */
    class LifecycleCallback : public ObjHandle {

        /**
         * Templated dispatch allows for capturing multiple arguments to pass to a callback
         * routine. Note that this enables the same callback approach as std::invoke and std::thread
         * where a typical use is (callbackMethod, this) but acceptable also to do something like
         * (callbackMethod, this, extraArg1, extraArg2).
         * @tparam Callable Lambda, function pointer, method, etc.
         * @tparam Args Prefix arguments, particularly optional This
         */
        template<typename Callable, typename... Args>
        class LifecycleDispatch : public CallbackManager::CallbackDispatch {

            const Callable _callable;
            const std::tuple<Args...> _args;

        public:
            explicit LifecycleDispatch(Callable callable, Args &&...args)
                : _callable{std::move(callable)}, _args{std::forward<Args>(args)...} {
                static_assert(
                    std::is_invocable_r_v<bool, Callable, Args..., ModuleScope, Symbol, Struct>);
            }
            [[nodiscard]] Symbol type() const override {
                return {"lifecycle"};
            }
            [[nodiscard]] CallbackManager::Delegate prepare(
                uint32_t callbackType, uint32_t size, const void *data) const override {
                assertCallbackType(Symbol(callbackType));
                auto &cb = checkedStruct<LifecycleCallbackData>(size, data);
                auto target = _callable;
                auto module = ModuleScope(cb.moduleHandle);
                auto phase = Symbol(cb.phaseSymbol);
                auto dataStruct = Struct(cb.dataStruct);
                auto args = std::tuple_cat(_args, std::tuple{module, phase, dataStruct});
                return [target, args]() {
                    bool f = std::apply(target, args);
                    return static_cast<uint32_t>(f);
                };
            }
        };

    public:
        constexpr LifecycleCallback() noexcept = default;

        explicit LifecycleCallback(const ObjHandle &other) : ObjHandle{other} {
        }

        /**
         * Create reference to a lifecycle callback.
         */
        template<typename Callable, typename... Args>
        static LifecycleCallback of(const Callable &callable, Args &&...args) {
            auto dispatch = std::make_unique<LifecycleDispatch<Callable, Args...>>(
                callable, std::forward<Args>(args)...);
            return CallbackManager::self().registerWithNucleus<LifecycleCallback>(
                std::move(dispatch));
        }
    };

    /**
     * Abstract channel listen callback
     */
    class ChannelListenCallback : public ObjHandle {

        /**
         * Templated dispatch allows for capturing multiple arguments to pass to a callback
         * routine. Note that this enables the same callback approach as std::invoke and std::thread
         * where a typical use is (callbackMethod, this) but acceptable also to do something like
         * (callbackMethod, this, extraArg1, extraArg2).
         * @tparam Callable Lambda, function pointer, method, etc.
         * @tparam Args Prefix arguments, particularly optional This
         */
        template<typename Callable, typename... Args>
        class ChannelListenDispatch : public CallbackManager::CallbackDispatch {

            const Callable _callable;
            const std::tuple<Args...> _args;

        public:
            explicit ChannelListenDispatch(Callable callable, Args &&...args)
                : _callable{std::move(callable)}, _args{std::forward<Args>(args)...} {
                static_assert(std::is_invocable_v<Callable, Args..., Struct>);
            }
            [[nodiscard]] Symbol type() const override {
                return {"channelListen"};
            }
            [[nodiscard]] CallbackManager::Delegate prepare(
                uint32_t callbackType, uint32_t size, const void *data) const override {
                assertCallbackType(Symbol(callbackType));
                auto &cb = checkedStruct<ChannelListenCallbackData>(size, data);
                auto callable = _callable;
                auto dataStruct = Struct(cb.dataStruct);
                auto args = std::tuple_cat(_args, std::tuple{dataStruct});
                return [callable, args]() {
                    std::apply(callable, args);
                    return static_cast<uint32_t>(true);
                };
            }
        };

    public:
        constexpr ChannelListenCallback() noexcept = default;

        explicit ChannelListenCallback(const ObjHandle &other) : ObjHandle{other} {
        }

        /**
         * Create reference to a channel listen callback.
         */
        template<typename Callable, typename... Args>
        static ChannelListenCallback of(const Callable &callable, Args &&...args) {
            auto dispatch = std::make_unique<ChannelListenDispatch<Callable, Args...>>(
                callable, std::forward<Args>(args)...);
            return CallbackManager::self().registerWithNucleus<ChannelListenCallback>(
                std::move(dispatch));
        }
    };

    /**
     * Abstract channel close callback
     */
    class ChannelCloseCallback : public ObjHandle {

        /**
         * Templated dispatch allows for capturing multiple arguments to pass to a callback
         * routine. Note that this enables the same callback approach as std::invoke and std::thread
         * where a typical use is (callbackMethod, this) but acceptable also to do something like
         * (callbackMethod, this, extraArg1, extraArg2).
         * @tparam Callable Lambda, function pointer, method, etc.
         * @tparam Args Prefix arguments, particularly optional This
         */
        template<typename Callable, typename... Args>
        class ChannelCloseDispatch : public CallbackManager::CallbackDispatch {

            const Callable _callable;
            const std::tuple<Args...> _args;

        public:
            explicit ChannelCloseDispatch(Callable callable, Args &&...args)
                : _callable{std::move(callable)}, _args{std::forward<Args>(args)...} {
                static_assert(std::is_invocable_v<Callable, Args...>);
            }
            [[nodiscard]] Symbol type() const override {
                return {"channelClose"};
            }
            [[nodiscard]] CallbackManager::Delegate prepare(
                uint32_t callbackType, uint32_t size, const void *data) const override {
                assertCallbackType(Symbol(callbackType));
                auto &cb = checkedStruct<ChannelCloseCallbackData>(size, data);
                auto callable = _callable;
                auto args = _args;
                return [callable, args]() {
                    std::apply(callable, args);
                    return static_cast<uint32_t>(true);
                };
            }
        };

    public:
        constexpr ChannelCloseCallback() noexcept = default;

        explicit ChannelCloseCallback(const ObjHandle &other) : ObjHandle{other} {
        }

        /**
         * Create reference to a channel close callback.
         */
        template<typename Callable, typename... Args>
        static ChannelCloseCallback of(const Callable &callable, Args &&...args) {
            auto dispatch = std::make_unique<ChannelCloseDispatch<Callable, Args...>>(
                callable, std::forward<Args>(args)...);
            return CallbackManager::self().registerWithNucleus<ChannelCloseCallback>(
                std::move(dispatch));
        }
    };

    inline Subscription Scope::subscribeToTopic(Symbol topic, TopicCallback callback) {
        required();
        return callApiReturnHandle<Subscription>([*this, topic, callback]() {
            return ::ggapiSubscribeToTopic(getHandleId(), topic.asInt(), callback.getHandleId());
        });
    }

    inline Subscription Scope::subscribeToTopic(Symbol topic, const TopicCallbackLambda &callback) {
        return subscribeToTopic(topic, TopicCallback::of(callback));
    }

    inline void Channel::addListenCallback(ChannelListenCallback callback) {
        required();
        callApi(
            [*this, callback]() { ::ggapiChannelListen(getHandleId(), callback.getHandleId()); });
    }

    template<typename Callback, typename... Args>
    inline void Channel::addListenCallback(const Callback callback, Args &&...args) {
        addListenCallback(ChannelListenCallback::of(callback, std::forward<Args>(args)...));
    }

    inline void Channel::addCloseCallback(ChannelCloseCallback callback) {
        required();
        callApi(
            [*this, callback]() { ::ggapiChannelOnClose(getHandleId(), callback.getHandleId()); });
    }

    template<typename Callback, typename... Args>
    inline void Channel::addCloseCallback(const Callback callback, Args &&...args) {
        addCloseCallback(ChannelCloseCallback::of(callback, std::forward<Args>(args)...));
    }

    inline Task Task::sendToTopicAsync(
        Symbol topic, Struct message, TopicCallback resultCallback, int32_t timeout) {

        return callApiReturnHandle<Task>([topic, message, resultCallback, timeout]() {
            return ::ggapiSendToTopicAsync(
                topic.asInt(), message.getHandleId(), resultCallback.getHandleId(), timeout);
        });
    }

    inline Task Task::sendToTopicAsync(
        Symbol topic, Struct message, const TopicCallbackLambda &resultCallback, int32_t timeout) {

        return sendToTopicAsync(topic, message, TopicCallback::of(resultCallback), timeout);
    }

    inline Struct Task::sendToTopic(ggapi::Symbol topic, Struct message, int32_t timeout) {
        return callApiReturnHandle<Struct>([topic, message, timeout]() {
            return ::ggapiSendToTopic(topic.asInt(), message.getHandleId(), timeout);
        });
    }

    inline Task Subscription::callAsync(
        Struct message, TopicCallback resultCallback, int32_t timeout) const {
        required();
        return callApiReturnHandle<Task>([this, message, resultCallback, timeout]() {
            return ::ggapiSendToListenerAsync(
                getHandleId(), message.getHandleId(), resultCallback.getHandleId(), timeout);
        });
    }

    inline Task Subscription::callAsync(
        Struct message, const TopicCallbackLambda &resultCallback, int32_t timeout) const {
        required();
        return callAsync(message, TopicCallback::of(resultCallback), timeout);
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

    inline void Task::sleep(uint32_t duration) {
        return callApi([duration]() { ::ggapiSleep(duration); });
    }

    inline void Task::cancelTask() {
        required();
        callApi([this]() { return ::ggapiCancelTask(getHandleId()); });
    }

    inline Task Task::current() {
        return callApiReturnHandle<Task>([]() { return ::ggapiGetCurrentTask(); });
    }

    inline Task Task::callAsync(Struct data, TaskCallback callback, uint32_t delay) {
        return callApiReturnHandle<Task>([data, callback, delay]() {
            return ::ggapiCallAsync(data.getHandleId(), callback.getHandleId(), delay);
        });
    }

    inline Task Task::callAsync(Struct data, const TaskCallbackLambda &callback, uint32_t delay) {
        return callAsync(data, TaskCallback::of(callback), delay);
    }

    inline ModuleScope ModuleScope::registerPlugin(
        Symbol componentName, LifecycleCallback callback) {
        required();
        return callApiReturnHandle<ModuleScope>([*this, componentName, callback]() {
            return ::ggapiRegisterPlugin(
                getHandleId(), componentName.asInt(), callback.getHandleId());
        });
    }

    inline ModuleScope ModuleScope::registerPlugin(
        Symbol componentName, const LifecycleCallbackLambda &callback) {
        return registerPlugin(componentName, LifecycleCallback::of(callback));
    }

    inline ModuleScope ModuleScope::registerGlobalPlugin(
        Symbol componentName, LifecycleCallback callback) {
        return callApiReturnHandle<ModuleScope>([componentName, callback]() {
            return ::ggapiRegisterPlugin(0, componentName.asInt(), callback.getHandleId());
        });
    }

    inline ModuleScope ModuleScope::registerGlobalPlugin(
        Symbol componentName, const LifecycleCallbackLambda &callback) {
        return registerGlobalPlugin(componentName, LifecycleCallback::of(callback));
    }

    inline Buffer Container::toJson() const {
        required();
        return callApiReturnHandle<Buffer>([*this]() { return ::ggapiToJson(_handle); });
    }

    inline Buffer Container::toYaml() const {
        required();
        return callApiReturnHandle<Buffer>([*this]() { return ::ggapiToYaml(_handle); });
    }

    //
    // Translated exception
    //
    class GgApiError : public std::runtime_error {
        Symbol _kind;

        template<typename Error>
        static Symbol typeKind() {
            static_assert(std::is_base_of_v<std::exception, Error>);
            return typeid(Error).name();
        }

    public:
        GgApiError(const GgApiError &) noexcept = default;
        GgApiError(GgApiError &&) noexcept = default;
        GgApiError &operator=(const GgApiError &) noexcept = default;
        GgApiError &operator=(GgApiError &&) noexcept = default;
        ~GgApiError() override = default;

        explicit GgApiError(
            Symbol kind = typeKind<GgApiError>(),
            const std::string &what = "Unspecified Error") noexcept
            : _kind(kind), std::runtime_error(what) {
        }

        template<typename E>
        static GgApiError of(const E &error) {
            static_assert(std::is_base_of_v<std::exception, E>);
            return GgApiError(typeKind<E>(), error.what());
        }

        [[nodiscard]] constexpr Symbol kind() const {
            return _kind;
        }

        void toThreadLastError() {
            toThreadLastError(_kind, what());
        }

        static void toThreadLastError(Symbol kind, std::string_view what) {
            ::ggapiSetError(kind.asInt(), what.data(), what.length());
        }

        static void clearThreadLastError() {
            ::ggapiSetError(0, nullptr, 0);
        }

        static std::optional<GgApiError> fromThreadLastError(bool clear = false) {
            auto lastErrorKind = ::ggapiGetErrorKind();
            if(lastErrorKind != 0) {
                Symbol sym(lastErrorKind);
                std::string copy{::ggapiGetErrorWhat()}; // before clear
                if(clear) {
                    clearThreadLastError();
                }
                return GgApiError(sym, copy);
            } else {
                return {};
            }
        }

        static bool hasThreadLastError() {
            return ::ggapiGetErrorKind() != 0;
        }

        static void throwIfThreadHasError() {
            // Wrap in fast check
            if(hasThreadLastError()) {
                // Slower path
                auto lastError = fromThreadLastError(true);
                if(lastError.has_value()) {
                    throw GgApiError(lastError.value());
                }
            }
        }
    };

    template<>
    inline GgApiError GgApiError::of<GgApiError>(const GgApiError &error) {
        return error;
    }

    //
    // Exceptions do not cross module borders - translate an exception into a thread error
    //
    template<typename T>
    inline T trapErrorReturn(const std::function<T()> &fn) noexcept {
        try {
            GgApiError::clearThreadLastError();
            if constexpr(std::is_void_v<T>) {
                fn();
            } else {
                return fn();
            }
        } catch(std::exception &e) {
            GgApiError::of(e).toThreadLastError();
        } catch(...) {
            GgApiError().toThreadLastError();
        }
        if constexpr(std::is_void_v<T>) {
            return;
        } else {
            return static_cast<T>(0);
        }
    }

    inline void callApi(const std::function<void()> &fn) {
        GgApiError::clearThreadLastError();
        fn();
        GgApiError::throwIfThreadHasError();
    }

    template<typename T>
    inline T callApiReturn(const std::function<T()> &fn) {
        if constexpr(std::is_void_v<T>) {
            callApi(fn);
        } else {
            GgApiError::clearThreadLastError();
            T v = fn();
            GgApiError::throwIfThreadHasError();
            return v;
        }
    }

    template<typename T>
    inline T callApiReturnHandle(const std::function<uint32_t()> &fn) {
        static_assert(std::is_base_of_v<ObjHandle, T>);
        return T(callApiReturn<uint32_t>(fn));
    }

} // namespace ggapi
