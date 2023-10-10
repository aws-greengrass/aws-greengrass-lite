#pragma once

#include <c_api.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

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
    class Subscription;
    class GgApiError; // error from GG API call

    typedef std::function<Struct(Scope, StringOrd, Struct)> topicCallbackLambda;
    typedef std::function<void(Scope, StringOrd, Struct)> lifecycleCallbackLambda;
    typedef Struct (*topicCallback_t)(Scope, StringOrd, Struct);
    typedef void (*lifecycleCallback_t)(Scope, StringOrd, Struct);
    uint32_t topicCallbackProxy(
        uintptr_t callbackContext, uint32_t taskHandle, uint32_t topicOrd, uint32_t dataStruct
    ) noexcept;
    bool lifecycleCallbackProxy(
        uintptr_t callbackContext, uint32_t moduleHandle, uint32_t phaseOrd, uint32_t dataStruct
    ) noexcept;
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
        size_t strLen, const std::function<size_t(char *, size_t)> &stringFillFn
    ) {
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

        explicit StringOrd(std::string_view sv) noexcept : _ord{intern(sv)} {
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
            return _ord == other._ord;
        }

        [[nodiscard]] constexpr uint32_t toOrd() const noexcept {
            return _ord;
        }

        [[nodiscard]] std::string toString() const {
            auto len =
                callApiReturn<size_t>([*this]() { return ::ggapiGetOrdinalStringLen(_ord); });
            return stringFillHelper(len, [*this](auto buf, auto bufLen) {
                return callApiReturn<size_t>([*this, &buf, bufLen]() {
                    return ::ggapiGetOrdinalString(_ord, buf, bufLen);
                });
            });
        }
    };

    //
    // All objects are passed by handle, this class abstracts the object handles
    //
    class ObjHandle {
    protected:
        uint32_t _handle{0};

    public:
        constexpr ObjHandle() noexcept = default;
        constexpr ObjHandle(const ObjHandle &) noexcept = default;
        constexpr ObjHandle(ObjHandle &&) noexcept = default;
        constexpr ObjHandle &operator=(const ObjHandle &) noexcept = default;
        constexpr ObjHandle &operator=(ObjHandle &&) noexcept = default;
        ~ObjHandle() noexcept = default;

        explicit constexpr ObjHandle(uint32_t handle) noexcept : _handle{handle} {
        }

        constexpr bool operator==(ObjHandle other) const {
            return _handle == other._handle;
        }

        constexpr bool operator!=(ObjHandle other) const {
            return _handle != other._handle;
        }

        constexpr explicit operator bool() const {
            return _handle != 0;
        }

        constexpr bool operator!() const {
            return _handle == 0;
        }

        //
        // Retrieve underlying handle ID
        //
        [[nodiscard]] constexpr uint32_t getHandleId() const {
            return _handle;
        }

        void release() const {
            callApi([*this]() { ::ggapiReleaseHandle(_handle); });
        }
    };

    //
    // Generic operations for all object types
    //
    template<typename T>
    class ObjectBase : public ObjHandle {
    public:
        ObjectBase() noexcept = default;
        ObjectBase(const ObjectBase &) noexcept = default;
        ObjectBase(ObjectBase &&) noexcept = default;
        ObjectBase &operator=(const ObjectBase &) noexcept = default;
        ObjectBase &operator=(ObjectBase &&) noexcept = default;
        ~ObjectBase() = default;

        explicit ObjectBase(const ObjHandle &other) noexcept : ObjHandle{other} {
            static_assert(std::is_base_of_v<ObjHandle, T>);
        }

        explicit ObjectBase(uint32_t handle) noexcept : ObjHandle{handle} {
        }

        //
        // Anchor this against another scope
        //
        [[nodiscard]] T anchor(Scope newParent) const;
    };

    //
    // Scopes are a class of handles that are used as targets for anchors
    //
    class Scope : public ObjectBase<Scope> {
    public:
        Scope() noexcept = default;
        Scope(const Scope &) noexcept = default;
        Scope(Scope &&) noexcept = default;
        Scope &operator=(const Scope &) noexcept = default;
        Scope &operator=(Scope &&) noexcept = default;
        ~Scope() = default;

        explicit Scope(const ObjHandle &other) noexcept : ObjectBase{other} {
        }

        explicit Scope(uint32_t handle) noexcept : ObjectBase{handle} {
        }

        [[nodiscard]] Subscription subscribeToTopic(StringOrd topic, topicCallback_t callback);
        [[nodiscard]] Scope sendToTopicAsync(
            StringOrd topic, Struct message, topicCallback_t result, int32_t timeout = -1
        );
        [[nodiscard]] static Struct sendToTopic(
            StringOrd topic, Struct message, int32_t timeout = -1
        );
        [[nodiscard]] Struct waitForTaskCompleted(int32_t timeout = -1);
        [[nodiscard]] Scope registerPlugin(StringOrd componentName, lifecycleCallback_t callback);
        [[nodiscard]] static Scope thisTask();

        [[nodiscard]] Struct createStruct();
        [[nodiscard]] List createList();
        [[nodiscard]] Buffer createBuffer();
    };

    //
    // Scopes are a class of handles that are used as targets for anchors
    //
    class ThreadScope : public Scope {
    public:
        ThreadScope() noexcept = default;
        ThreadScope(const ThreadScope &) noexcept = default;
        ThreadScope(ThreadScope &&) noexcept = default;
        ThreadScope &operator=(const ThreadScope &) noexcept = default;
        ThreadScope &operator=(ThreadScope &&) noexcept = default;

        explicit ThreadScope(uint32_t handle) noexcept : Scope{handle} {
        }

        [[nodiscard]] static ThreadScope claimThread();

        static void releaseThread() {
            return callApi([]() { ::ggapiReleaseThread(); });
        }

        ~ThreadScope() {
            ::ggapiReleaseThread();
        }
    };

    //
    // Scopes are a class of handles that are used as targets for anchors
    //
    class Subscription : public ObjectBase<Subscription> {
    public:
        explicit Subscription(const ObjHandle &other) : ObjectBase{other} {
        }

        explicit Subscription(uint32_t handle) : ObjectBase{handle} {
        }
    };

    //
    // Containers are the root for Structures and Lists
    //
    class Container : public ObjHandle {
    private:
    public:
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
    private:
    public:
        explicit Struct(const ObjHandle &other) : Container{other} {
        }

        explicit Struct(uint32_t handle) : Container{handle} {
        }

        static Struct create(ObjHandle parent) {
            return Struct(::ggapiCreateStruct(parent.getHandleId()));
        }

        template<typename T>
        Struct &put(StringOrd ord, T v) {
            if constexpr(std::is_integral_v<T>) {
                auto intv = static_cast<uint64_t>(v);
                callApi([*this, ord, intv]() { ::ggapiStructPutInt64(_handle, ord.toOrd(), intv); }
                );
            } else if constexpr(std::is_floating_point_v<T>) {
                auto floatv = static_cast<double>(v);
                callApi([*this, ord, floatv]() {
                    ::ggapiStructPutFloat64(_handle, ord.toOrd(), floatv);
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
            } else {
                static_assert(T::usingUnsupportedType, "Unsupported type");
            }
            return *this;
        }

        template<typename T>
        Struct &put(std::string_view sv, T v) {
            return put<T>(StringOrd(sv), v);
        }

        [[nodiscard]] bool hasKey(StringOrd ord) const {
            return callApiReturn<bool>([*this, ord]() {
                return ::ggapiStructHasKey(_handle, ord.toOrd());
            });
        }

        [[nodiscard]] bool hasKey(std::string_view sv) const {
            return hasKey(StringOrd{sv});
        }

        template<typename T>
        T get(StringOrd ord) {
            if constexpr(std::is_integral_v<T>) {
                auto intv = callApiReturn<uint64_t>([*this, ord]() {
                    return ::ggapiStructGetInt64(_handle, ord.toOrd());
                });
                return static_cast<T>(intv);
            } else if constexpr(std::is_floating_point_v<T>) {
                auto floatv = callApiReturn<double>([*this, ord]() {
                    return ::ggapiStructGetFloat64(_handle, ord.toOrd());
                });
                return static_cast<T>(floatv);
            } else if constexpr(std::is_base_of_v<std::string, T>) {
                size_t len = callApiReturn<size_t>([*this, ord]() {
                    return ::ggapiStructGetStringLen(_handle, ord.toOrd());
                });
                return stringFillHelper(len, [*this, ord](auto buf, auto bufLen) {
                    return callApiReturn<size_t>([*this, ord, &buf, bufLen]() {
                        return ::ggapiStructGetString(_handle, ord.toOrd(), buf, bufLen);
                    });
                });
            } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
                return callApiReturnHandle<T>([*this, ord]() {
                    return ::ggapiStructGetHandle(_handle, ord.toOrd());
                });
            } else {
                static_assert(T::usingUnsupportedType, "Unsupported type");
            }
        }

        template<typename T>
        T get(std::string_view sv) {
            return get<T>(StringOrd(sv));
        }
    };

    //
    // Lists are containers with index-based keys
    //
    class List : public Container {
    private:
    public:
        explicit List(const ObjHandle &other) : Container{other} {
        }

        explicit List(uint32_t handle) : Container{handle} {
        }

        static List create(ObjHandle parent) {
            return List(::ggapiCreateStruct(parent.getHandleId()));
        }

        template<typename T>
        List &put(int32_t idx, T v) {
            if constexpr(std::is_integral_v<T>) {
                auto intv = static_cast<uint64_t>(v);
                callApi([*this, idx, intv]() { ::ggapiListPutInt64(_handle, idx, intv); });
            } else if constexpr(std::is_floating_point_v<T>) {
                auto floatv = static_cast<double>(v);
                callApi([*this, idx, floatv]() { ::ggapiListPutFloat64(_handle, idx, floatv); });
            } else if constexpr(std::is_constructible_v<std::string_view, T>) {
                std::string_view str(v);
                callApi([*this, idx, str]() {
                    ::ggapiListPutString(_handle, idx, str.data(), str.length());
                });
            } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
                callApi([*this, idx, &v]() { ::ggapiListPutHandle(_handle, idx, v.getHandleId()); }
                );
            } else {
                static_assert(T::usingUnsupportedType, "Unsupported type");
            }
            return *this;
        }

        template<typename T>
        List &insert(int32_t idx, T v) {
            if constexpr(std::is_integral_v<T>) {
                auto intv = static_cast<uint64_t>(v);
                callApi([*this, idx, intv]() { ::ggapiListInsertInt64(_handle, idx, intv); });
            } else if constexpr(std::is_floating_point_v<T>) {
                auto floatv = static_cast<double>(v);
                callApi([*this, idx, floatv]() { ::ggapiListInsertFloat64(_handle, idx, floatv); });
            } else if constexpr(std::is_constructible_v<std::string_view, T>) {
                std::string_view str(v);
                callApi([*this, idx, str]() {
                    ::ggapiListInsertString(_handle, idx, str.data(), str.length());
                });
            } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
                callApi([*this, idx, &v]() {
                    ::ggapiListInsertHandle(_handle, idx, v.getHandleId());
                });
            } else {
                static_assert(T::usingUnsupportedType, "Unsupported type");
            }
            return *this;
        }

        template<typename T>
        T get(int32_t idx) {
            if constexpr(std::is_integral_v<T>) {
                auto intv = callApiReturn<uint64_t>([*this, idx]() {
                    return ::ggapiListGetInt64(_handle, idx);
                });
                return static_cast<T>(intv);
            } else if constexpr(std::is_floating_point_v<T>) {
                auto floatv = callApiReturn<double>([*this, idx]() {
                    return ::ggapiListGetFloat64(_handle, idx);
                });
                return static_cast<T>(floatv);
            } else if constexpr(std::is_base_of_v<std::string, T>) {
                size_t len = callApiReturn<size_t>([*this, idx]() {
                    return ::ggapiListGetStringLen(_handle, idx);
                });
                return stringFillHelper(len, [*this, idx](auto buf, auto bufLen) {
                    return callApiReturn<size_t>([*this, idx, &buf, bufLen]() {
                        return ::ggapiListGetString(_handle, idx, buf, bufLen);
                    });
                });
            } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
                return callApiReturnHandle<T>([*this, idx]() {
                    return ::ggapiListGetHandle(_handle, idx);
                });
            } else {
                static_assert(T::usingUnsupportedType, "Unsupported type");
            }
        }
    };

    //
    // Buffers are shared containers of bytes
    //
    class Buffer : public Container {
    private:
    public:
        typedef uint8_t Byte;
        typedef std::vector<Byte> Vector;

        explicit Buffer(const ObjHandle &other) : Container{other} {
        }

        explicit Buffer(uint32_t handle) : Container{handle} {
        }

        static Buffer create(ObjHandle parent) {
            return Buffer(::ggapiCreateBuffer(parent.getHandleId()));
        }

        Buffer &put(int32_t idx, const Vector &vec) {
            callApi([*this, idx, &vec]() { ::ggapiBufferPut(_handle, idx, vec.data(), vec.size()); }
            );
            return *this;
        }

        Buffer &insert(int32_t idx, const Vector &vec) {
            callApi([*this, idx, &vec]() {
                ::ggapiBufferInsert(_handle, idx, vec.data(), vec.size());
            });
            return *this;
        }

        Buffer &get(int32_t idx, Vector &vec) {
            callApi([*this, idx, &vec]() {
                uint32_t actLen = ::ggapiBufferGet(_handle, idx, vec.data(), vec.size());
                vec.resize(actLen);
            });
            return *this;
        }

        Buffer &resize(uint32_t newSize) {
            callApi([*this, newSize]() { ::ggapiBufferResize(_handle, newSize); });
            return *this;
        }
    };

    template<typename T>
    inline T ObjectBase<T>::anchor(Scope newParent) const {
        return callApiReturnHandle<T>([newParent, *this]() {
            return ::ggapiAnchorHandle(newParent.getHandleId(), _handle);
        });
    }

    inline ThreadScope ThreadScope::claimThread() {
        return callApiReturnHandle<ThreadScope>([]() { return ::ggapiClaimThread(); });
    }

    inline Struct Scope::createStruct() {
        return Struct::create(*this);
    }

    inline List Scope::createList() {
        return List::create(*this);
    }

    inline Buffer Scope::createBuffer() {
        return Buffer::create(*this);
    }

    inline Subscription Scope::subscribeToTopic(StringOrd topic, topicCallback_t callback) {
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

    inline Scope Scope::sendToTopicAsync(
        StringOrd topic, Struct message, topicCallback_t result, int32_t timeout
    ) {
        return callApiReturnHandle<Scope>([topic, message, result, timeout]() {
            return ::ggapiSendToTopicAsync(
                topic.toOrd(),
                message.getHandleId(),
                topicCallbackProxy,
                reinterpret_cast<uintptr_t>(result), // NOLINT(*-reinterpret-cast)
                timeout
            );
        });
    }

    inline Struct Scope::sendToTopic(ggapi::StringOrd topic, Struct message, int32_t timeout) {
        return callApiReturnHandle<Struct>([topic, message, timeout]() {
            return ::ggapiSendToTopic(topic.toOrd(), message.getHandleId(), timeout);
        });
    }

    inline Struct Scope::waitForTaskCompleted(int32_t timeout) {
        return callApiReturnHandle<Struct>([*this, timeout]() {
            return ::ggapiWaitForTaskCompleted(getHandleId(), timeout);
        });
    }

    inline Scope Scope::thisTask() {
        return callApiReturnHandle<Scope>([]() { return ::ggapiGetCurrentTask(); });
    }

    inline Scope Scope::registerPlugin(StringOrd componentName, lifecycleCallback_t callback) {
        return callApiReturnHandle<Scope>([*this, componentName, callback]() {
            return ::ggapiRegisterPlugin(
                getHandleId(),
                componentName.toOrd(),
                lifecycleCallbackProxy,
                reinterpret_cast<uintptr_t>(callback) // NOLINT(*-reinterpret-cast)
            );
        });
    }

    inline uint32_t topicCallbackProxy(
        uintptr_t callbackContext, uint32_t taskHandle, uint32_t topicOrd, uint32_t dataStruct
    ) noexcept {
        return trapErrorReturn<uint32_t>([callbackContext, taskHandle, topicOrd, dataStruct]() {
            auto callback =
                reinterpret_cast<topicCallback_t>(callbackContext); // NOLINT(*-reinterpret-cast)
            return callback(Scope{taskHandle}, StringOrd{topicOrd}, Struct{dataStruct})
                .getHandleId();
        });
    }

    inline bool lifecycleCallbackProxy(
        uintptr_t callbackContext, uint32_t moduleHandle, uint32_t phaseOrd, uint32_t dataStruct
    ) noexcept {
        return trapErrorReturn<bool>([callbackContext, moduleHandle, phaseOrd, dataStruct]() {
            // NOLINTNEXTLINE(*-reinterpret-cast)
            auto callback = reinterpret_cast<lifecycleCallback_t>(callbackContext);
            callback(Scope{moduleHandle}, StringOrd{phaseOrd}, Struct{dataStruct});
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
