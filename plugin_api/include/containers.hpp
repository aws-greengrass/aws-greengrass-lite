#pragma once

#include "api_forwards.hpp"
#include "buffer_stream.hpp"
#include "c_api.hpp"
#include "handles.hpp"
#include "util.hpp"
#include <optional>
#include <span.hpp>
#include <variant>

namespace ggapi {
    class Promise;

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
        static Container boxImpl(const Symbol &v) {
            return callApiReturnHandle<Container>([v]() { return ::ggapiBoxSymbol(v.asInt()); });
        }
        static Container boxImpl(const ObjHandle &v) {
            return callApiReturnHandle<Container>(
                [v]() { return ::ggapiBoxHandle(v.getHandleId()); });
        }

    public:
        [[nodiscard]] static bool isA(const ObjHandle &obj) {
            return obj.isContainer();
        }

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
                    // Normally casting and returning string_view is bad, however the passed in
                    // string_view will be in scope for as long as this returned value is used.
                    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
                    return static_cast<std::string_view>(x);
                } else {
                    return x;
                }
            }

            template<typename T>
            std::optional<T> checkedObject() const {
                static_assert(std::is_base_of_v<ObjHandle, T>);
                if(!std::holds_alternative<ObjHandle>(*this)) {
                    return {};
                }
                ObjHandle obj = std::get<ObjHandle>(*this);
                if(T::isType(obj)) {
                    return {T{obj}};
                } else {
                    return {};
                }
            }
        };

        using KeyValue = std::pair<Symbol, ArgValue>;

        constexpr Container() noexcept = default;

        explicit Container(const ObjHandle &other) : ObjHandle{other} {
        }

        explicit Container(const SharedHandle &handle) : ObjHandle{handle} {
        }

        [[nodiscard]] Container clone() const {
            return callHandleApiThrowError<Container>(::ggapiCloneContainer, asId());
        }

        [[nodiscard]] uint32_t size() const {
            return callApiReturn<uint32_t>([this]() { return ::ggapiGetSize(asId()); });
        }

        [[nodiscard]] bool empty() const {
            return callApiReturn<bool>([this]() { return ::ggapiIsEmpty(asId()); });
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
        [[nodiscard]] static Container box(const T &v) {
            return visitArg([](auto &&v) -> Container { return boxImpl(v); }, v);
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
                return callApiReturn<bool>([this]() { return ::ggapiUnboxBool(asId()); });
            } else if constexpr(std::is_integral_v<T>) {
                auto intVal =
                    callApiReturn<uint64_t>([this]() { return ::ggapiUnboxInt64(asId()); });
                return static_cast<T>(intVal);
            } else if constexpr(std::is_floating_point_v<T>) {
                auto floatVal =
                    callApiReturn<double>([this]() { return ::ggapiUnboxFloat64(asId()); });
                return static_cast<T>(floatVal);
            } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
                return callApiReturnHandle<T>([this]() { return ::ggapiUnboxHandle(asId()); });
            } else if constexpr(std ::is_assignable_v<T, std::string>) {
                size_t len =
                    callApiReturn<size_t>([this]() { return ::ggapiUnboxStringLen(asId()); });
                return stringFillHelper(len, [this](auto buf, auto bufLen) {
                    return callApiReturn<size_t>(
                        [this, &buf, bufLen]() { return ::ggapiUnboxString(asId(), buf, bufLen); });
                });
            } else {
                static_assert(T::usingUnsupportedType, "Unsupported type");
            }
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
            callApi([this, idx, v]() { ::ggapiListPutBool(asId(), idx, v); });
        }
        void putImpl(int32_t idx, uint64_t v) {
            callApi([this, idx, v]() { ::ggapiListPutInt64(asId(), idx, v); });
        }
        void putImpl(int32_t idx, double v) {
            callApi([this, idx, v]() { ::ggapiListPutFloat64(asId(), idx, v); });
        }
        void putImpl(int32_t idx, Symbol v) {
            callApi([this, idx, v]() { ::ggapiListPutSymbol(asId(), idx, v.asInt()); });
        }
        void putImpl(int32_t idx, std::string_view v) {
            callApi([this, idx, v]() { ::ggapiListPutString(asId(), idx, v.data(), v.length()); });
        }
        void putImpl(int32_t idx, const ObjHandle &v) {
            callApi([this, idx, v]() { ::ggapiListPutHandle(asId(), idx, v.getHandleId()); });
        }
        void insertImpl(int32_t idx, bool v) {
            callApi([this, idx, v]() { ::ggapiListInsertBool(asId(), idx, v); });
        }
        void insertImpl(int32_t idx, uint64_t v) {
            callApi([this, idx, v]() { ::ggapiListInsertInt64(asId(), idx, v); });
        }
        void insertImpl(int32_t idx, double v) {
            callApi([this, idx, v]() { ::ggapiListInsertFloat64(asId(), idx, v); });
        }
        void insertImpl(int32_t idx, Symbol v) {
            callApi([this, idx, v]() { ::ggapiListInsertSymbol(asId(), idx, v.asInt()); });
        }
        void insertImpl(int32_t idx, std::string_view v) {
            callApi(
                [this, idx, v]() { ::ggapiListInsertString(asId(), idx, v.data(), v.length()); });
        }
        void insertImpl(int32_t idx, const ObjHandle &v) {
            callApi([this, idx, v]() { ::ggapiListInsertHandle(asId(), idx, v.getHandleId()); });
        }

    public:
        [[nodiscard]] static bool isA(const ObjHandle &obj) {
            return obj.isList();
        }

        constexpr List() noexcept = default;

        explicit List(const ObjHandle &other) : Container{other} {
            check();
        }

        explicit List(const SharedHandle &handle) : Container{handle} {
            check();
        }

        static List create() {
            return callHandleApiThrowError<List>(::ggapiCreateList);
        }

        [[nodiscard]] List clone() const {
            return callHandleApiThrowError<List>(::ggapiCloneContainer, asId());
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
        [[nodiscard]] T get(int32_t idx) const {
            required();
            if constexpr(std::is_same_v<bool, T>) {
                return callApiReturn<bool>(
                    [this, idx]() { return ::ggapiListGetBool(asId(), idx); });
            } else if constexpr(std::is_integral_v<T>) {
                auto intVal = callApiReturn<uint64_t>(
                    [this, idx]() { return ::ggapiListGetInt64(asId(), idx); });
                return static_cast<T>(intVal);
            } else if constexpr(std::is_floating_point_v<T>) {
                auto floatVal = callApiReturn<double>(
                    [this, idx]() { return ::ggapiListGetFloat64(asId(), idx); });
                return static_cast<T>(floatVal);
            } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
                return callApiReturnHandle<T>(
                    [this, idx]() { return ::ggapiListGetHandle(asId(), idx); });
            } else if constexpr(std ::is_assignable_v<T, std::string>) {
                size_t len = callApiReturn<size_t>(
                    [this, idx]() { return ::ggapiListGetStringLen(asId(), idx); });
                return stringFillHelper(len, [this, idx](auto buf, auto bufLen) {
                    return callApiReturn<size_t>([this, idx, &buf, bufLen]() {
                        return ::ggapiListGetString(asId(), idx, buf, bufLen);
                    });
                });
            } else {
                static_assert(T::usingUnsupportedType, "Unsupported type");
            }
        }

        /**
         * Create a vector from this list. Assumes list will not be modified (call clone to enforce
         * this if needed)
         * @return vector of values
         */
        template<typename T>
        [[nodiscard]] std::vector<T> toVector() const {
            std::vector<T> values;
            auto count = util::safeBoundPositive<int32_t>(size());
            values.reserve(count);
            for(int32_t i = 0; i < count; ++i) {
                auto f = get<T>(i);
                values.emplace_back(f);
            }
            return values;
        }
    };

    /**
     * Structures are containers with associative keys.
     */
    class Struct : public Container {
        void check() {
            if(getHandleId() != 0 && !Container::isStruct()) {
                throw std::runtime_error("Structure handle expected");
            }
        }
        void putImpl(Symbol key, bool v) {
            callApi([this, key, v]() { ::ggapiStructPutBool(asId(), key.asInt(), v); });
        }
        void putImpl(Symbol key, uint64_t v) {
            callApi([this, key, v]() { ::ggapiStructPutInt64(asId(), key.asInt(), v); });
        }
        void putImpl(Symbol key, double v) {
            callApi([this, key, v]() { ::ggapiStructPutFloat64(asId(), key.asInt(), v); });
        }
        void putImpl(Symbol key, Symbol v) {
            callApi([this, key, v]() { ::ggapiStructPutSymbol(asId(), key.asInt(), v.asInt()); });
        }
        void putImpl(Symbol key, std::string_view v) {
            callApi([this, key, v]() {
                ::ggapiStructPutString(asId(), key.asInt(), v.data(), v.length());
            });
        }
        void putImpl(Symbol key, const ObjHandle &v) {
            callApi(
                [this, key, v]() { ::ggapiStructPutHandle(asId(), key.asInt(), v.getHandleId()); });
        }

    public:
        [[nodiscard]] static bool isA(const ObjHandle &obj) {
            return obj.isStruct();
        }

        constexpr Struct() noexcept = default;

        explicit Struct(const ObjHandle &other) : Container{other} {
            check();
        }

        explicit Struct(const SharedHandle &handle) : Container{handle} {
            check();
        }

        static Struct create() {
            return callHandleApiThrowError<Struct>(::ggapiCreateStruct);
        }

        [[nodiscard]] Struct clone() const {
            return callHandleApiThrowError<Struct>(::ggapiCloneContainer, asId());
        }

        /**
         * Find a key that matches specified key while ignoring case
         *
         * @param key case insensitive key
         * @return folded key (case sensitive)
         */
        Symbol foldKey(Symbol key) {
            ggapiSymbol symId = 0;
            callApiThrowError(::ggapiStructFoldKey, asId(), key.asInt(), &symId);
            return Symbol(symId);
        }

        /**
         * Creates a new structure that will subsequently be inserted as a child into this
         * structure, but is otherwise empty.
         * @return new structure
         */
        [[nodiscard]] Struct createForChild() const {
            return callHandleApiThrowError<Struct>(::ggapiStructCreateForChild, asId());
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

        [[nodiscard]] List keys() const {
            return callApiReturnHandle<List>([this]() { return ::ggapiStructKeys(asId()); });
        }

        [[nodiscard]] bool hasKey(Symbol key) const {
            required();
            return callApiReturn<bool>(
                [this, key]() { return ::ggapiStructHasKey(asId(), key.asInt()); });
        }

        using ObjHandle::isStruct;

        [[nodiscard]] bool isStruct(Symbol key) const {
            return get<ObjHandle>(key).isStruct();
        }

        template<typename T>
        [[nodiscard]] T get(Symbol key) const {
            required();
            if constexpr(std::is_same_v<bool, T>) {
                return callApiReturn<bool>(
                    [this, key]() { return ::ggapiStructGetBool(asId(), key.asInt()); });
            } else if constexpr(std::is_integral_v<T>) {
                auto intVal = callApiReturn<uint64_t>(
                    [this, key]() { return ::ggapiStructGetInt64(asId(), key.asInt()); });
                return static_cast<T>(intVal);
            } else if constexpr(std::is_floating_point_v<T>) {
                auto floatVal = callApiReturn<double>(
                    [this, key]() { return ::ggapiStructGetFloat64(asId(), key.asInt()); });
                return static_cast<T>(floatVal);
            } else if constexpr(std::is_base_of_v<ObjHandle, T>) {
                return callApiReturnHandle<T>(
                    [this, key]() { return ::ggapiStructGetHandle(asId(), key.asInt()); });
            } else if constexpr(std ::is_assignable_v<T, std::string>) {
                auto len = callApiReturn<size_t>(
                    [this, key]() { return ::ggapiStructGetStringLen(asId(), key.asInt()); });
                return stringFillHelper<typename T::traits_type, typename T::allocator_type>(
                    len, [this, key](auto buf, auto bufLen) {
                        return callApiReturn<size_t>([this, key, &buf, bufLen]() {
                            return ::ggapiStructGetString(asId(), key.asInt(), buf, bufLen);
                        });
                    });
            } else {
                static_assert(T::usingUnsupportedType, "Unsupported type");
            }
        }

        template<typename T, typename V>
        [[nodiscard]] T getValue(const std::initializer_list<V> &keys) const {
            ggapi::Struct childStruct = *this;
            auto it = keys.begin();
            for(; it != std::prev(keys.end()); it++) {
                childStruct = childStruct.get<ggapi::Struct>(*it);
            }
            return childStruct.get<T>(*it);
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
        [[nodiscard]] static bool isA(const ObjHandle &obj) {
            return obj.isBuffer();
        }

        constexpr Buffer() noexcept = default;

        explicit Buffer(const ObjHandle &other) : Container{other} {
            check();
        }

        explicit Buffer(const SharedHandle &handle) : Container{handle} {
            check();
        }

        static Buffer create() {
            return callHandleApiThrowError<Buffer>(::ggapiCreateBuffer);
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
            callApi([this, idx, bytes]() {
                ::ggapiBufferPut(asId(), idx, bytes.data(), bytes.size());
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
            callApi([this, idx, bytes]() {
                ::ggapiBufferInsert(asId(), idx, bytes.data(), bytes.size());
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
        [[nodiscard]] uint32_t get(int32_t idx, util::Span<DataT, SizeT> span) const {
            return get(idx, util::as_writeable_bytes(span)) / sizeof(DataT);
        }

        template<typename SizeT>
        [[nodiscard]] uint32_t get(int32_t idx, util::Span<char, SizeT> bytes) const {
            if(bytes.size() > std::numeric_limits<uint32_t>::max()) {
                throw std::out_of_range("length out of range");
            }

            required();
            return callApiReturn<uint32_t>([this, idx, bytes]() -> uint32_t {
                return ::ggapiBufferGet(asId(), idx, bytes.data(), bytes.size());
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
        [[nodiscard]] T get() const {
            return get<T>(0, size());
        }

        template<typename T>
        [[nodiscard]] T get(int32_t idx, size_t max) const {
            if(max > std::numeric_limits<uint32_t>::max()) {
                throw std::out_of_range("max length out of range");
            }
            T buffer;
            buffer.resize(std::min<size_t>(size(), max));
            get(idx, buffer);
            return buffer;
        }

        Buffer read(std::istream &is) {
            std::string ss(std::istreambuf_iterator<char>(is), {});
            return put(0, util::Span{ss.data(), ss.size()});
        }

        void write(std::ostream &os) const {
            auto buffer = get<std::vector<char>>(0, size());
            os.write(buffer.data(), buffer.size()); // NOLINT(*-narrowing-conversions)
        }

        Buffer &resize(uint32_t newSize) {
            required();
            callApi([this, newSize]() { ::ggapiBufferResize(asId(), newSize); });
            return *this;
        }

        /**
         * Parse buffer as if a JSON string. Type of container depends on type of JSON structure.
         */
        [[nodiscard]] Container fromJson() const {
            required();
            return callApiReturnHandle<Container>([this]() { return ::ggapiFromJson(asId()); });
        }

        /**
         * Parse buffer as if a YAML string. Type of container depends on type of YAML structure.
         */
        [[nodiscard]] Container fromYaml() const {
            required();
            return callApiReturnHandle<Container>([this]() { return ::ggapiFromYaml(asId()); });
        }
    };

    inline BufferStream Buffer::stream() {
        return BufferStream(*this);
    }

    inline BufferInStream Buffer::in() {
        return BufferInStream(stream());
    }

    inline BufferOutStream Buffer::out() {
        return BufferOutStream(stream());
    }

    inline Buffer Container::toJson() const {
        required();
        return callApiReturnHandle<Buffer>([this]() { return ::ggapiToJson(asId()); });
    }

    inline Buffer Container::toYaml() const {
        required();
        return callApiReturnHandle<Buffer>([this]() { return ::ggapiToYaml(asId()); });
    }

} // namespace ggapi
