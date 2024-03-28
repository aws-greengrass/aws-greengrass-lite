#pragma once
#include "data/handle_table.hpp"
#include "data/serializable.hpp"
#include "data/string_table.hpp"
#include "data/value_type.hpp"
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string_util.hpp>
#include <vector>

namespace scope {
    class Context;
}

namespace data {
    class ContainerModelBase;
    class StructModelBase;
    class ListModelBase;
    class SharedBuffer;
    class Boxed;

    /**
     * Data storage element with implicit type conversion. Implicit type conversion is necessary is
     * we deserialize from various formats that will often represent all types as strings.
     */
    class StructElement : public ValueTypes, public Serializable {
        friend class StructModelBase;

    protected:
        // size of variant is prob ~ 32 bytes or so, because of std::string
        // in theory, can be optimized, but is it worth it?
        ValueType _value;

        // used below but also used by debugging
        const std::string &rawGetString() const {
            return std::get<std::string>(_value);
        }

        // used below but also used by debugging
        const Symbol &rawGetSymbol() const {
            return std::get<Symbol>(_value);
        }

    public:
        StructElement() : _value{} {
        }

        template<typename T>
        // NOLINTNEXTLINE(*-explicit-constructor)
        StructElement(const T &v) : _value(v) {
            static_assert(
                std::is_assignable_v<ValueType, T>, "Must be a ValueType permitted value");
        }

        StructElement(const StructElement &) = default;
        StructElement(StructElement &&) = default;
        StructElement &operator=(const StructElement &el) = default;
        StructElement &operator=(StructElement &&el) noexcept = default;
        ~StructElement() noexcept override = default;

        [[nodiscard]] virtual bool empty() const {
            // Note, we don't do implicit operators for this to avoid confusion with bool
            return _value.index() == NONE;
        }

        void visit(Archive &archive) override;

        ValueType get() const {
            return _value;
        }

        [[nodiscard]] int getType() const {
            return static_cast<int>(_value.index());
        }

        StructElement &set(ValueType value) {
            // assumes detached element and not worried about cycles until this is inserted into a
            // list or struct
            _value = std::move(value);
            return *this;
        }

        [[nodiscard]] bool isObject() const {
            return _value.index() == OBJECT;
        }

        [[nodiscard]] bool isContainer() const {
            return isType<ContainerModelBase>();
        }

        [[nodiscard]] bool isBoxed() const {
            return isType<Boxed>();
        }

        [[nodiscard]] bool isStruct() const {
            return isType<StructModelBase>();
        }

        [[nodiscard]] bool isList() const {
            return isType<ListModelBase>();
        }

        [[nodiscard]] bool isScalar() const {
            return !isObject() && !isNull();
        }

        [[nodiscard]] bool isNull() const {
            return _value.index() == NONE;
        }

        [[nodiscard]] static bool getBool(std::string_view str) {
            if(str.empty()) {
                return false;
            }
            std::string s{util::lower(str)};
            return s != "false" && s != "no" && s != "0" && s != "0.0";
        }

        [[nodiscard]] bool getBool() const {
            switch(_value.index()) {
                case NONE:
                    return false;
                case BOOL:
                    return std::get<bool>(_value);
                case INT:
                    return std::get<uint64_t>(_value) != 0;
                case DOUBLE:
                    return std::get<double>(_value) != 0.0;
                case STRING:
                    return getBool(rawGetString());
                case SYMBOL: {
                    return getBool(rawGetSymbol().toString());
                }
                default:
                    return autoUnbox("boolean").getBool();
            }
        }

        [[nodiscard]] StructElement unbox() const;
        [[nodiscard]] StructElement autoUnbox(std::string_view desiredTypeForError) const;

        [[nodiscard]] uint64_t getInt() const {
            switch(_value.index()) {
                case NONE:
                    return 0;
                case BOOL:
                    return std::get<bool>(_value) ? 1 : 0;
                case INT:
                    return std::get<uint64_t>(_value);
                case DOUBLE:
                    return static_cast<uint64_t>(std::get<double>(_value));
                case STRING:
                    return std::stoul(rawGetString());
                case SYMBOL:
                    return std::stoul(rawGetSymbol().toString());
                default:
                    return autoUnbox("integer").getInt();
            }
        }

        [[nodiscard]] double getDouble() const {
            switch(_value.index()) {
                case NONE:
                    return std::numeric_limits<double>::quiet_NaN();
                case BOOL:
                    return std::get<bool>(_value) ? 1.0 : 0.0;
                case INT:
                    return static_cast<double>(std::get<uint64_t>(_value));
                case DOUBLE:
                    return std::get<double>(_value);
                case STRING:
                    return std::stod(rawGetString());
                case SYMBOL:
                    return std::stod(rawGetSymbol().toString());
                default:
                    return autoUnbox("double").getDouble();
            }
        }

        [[nodiscard]] std::string getString() const {
            switch(_value.index()) {
                case NONE:
                    return {};
                case BOOL:
                    return std::get<bool>(_value) ? "true" : "false";
                case INT:
                    return std::to_string(std::get<uint64_t>(_value));
                case DOUBLE:
                    return std::to_string(std::get<double>(_value));
                case STRING:
                    return rawGetString();
                case SYMBOL:
                    return rawGetSymbol().toString();
                default:
                    return autoUnbox("string").getString();
            }
        }

        [[nodiscard]] size_t getString(util::Span<char> span) const {
            // TODO: Enable future optimization
            std::string s = getString();
            if(s.length() > span.size()) {
                throw std::runtime_error("Destination buffer is too small");
            }
            return span.copyFrom(s.begin(), s.end());
        }

        [[nodiscard]] size_t getStringLen() const {
            // TODO: Enable future optimization
            return getString().length();
        }

        [[nodiscard]] std::shared_ptr<ContainerModelBase> getBoxed() const;

        [[nodiscard]] std::shared_ptr<TrackedObject> getObject() const;

        [[nodiscard]] std::shared_ptr<ContainerModelBase> getContainer() const {
            return castObject<ContainerModelBase>();
        }

        [[nodiscard]] std::shared_ptr<StructModelBase> getStruct() const {
            return castObject<StructModelBase>();
        }

        template<typename T>
        [[nodiscard]] bool isType() const {
            static_assert(std::is_base_of_v<TrackedObject, T>);
            if(!isObject()) {
                return false;
            }
            std::shared_ptr<T> p = std::dynamic_pointer_cast<T>(getObject());
            return static_cast<bool>(p);
        }

        template<typename T>
        [[nodiscard]] std::shared_ptr<T> castObject() const {
            static_assert(std::is_base_of_v<TrackedObject, T>);
            auto obj = getObject();
            if(!obj) {
                return {};
            }
            std::shared_ptr<T> p = std::dynamic_pointer_cast<T>(obj);
            if(p) {
                return p;
            } else {
                throw std::bad_cast();
            }
        }

        explicit operator uint64_t() const {
            return getInt();
        }

        explicit operator uint32_t() const {
            return static_cast<uint32_t>(getInt());
        }

        explicit operator double() const {
            return getDouble();
        }

        explicit operator float() const {
            return static_cast<float>(getDouble());
        }

        explicit operator std::string() const {
            return getString();
        }
    };

    //
    // Base class for classes that behave like a container - lists, structures and
    // buffers
    //
    class ContainerModelBase : public TrackedObject, public Serializable {
    public:
        using BadCastError = errors::InvalidContainerError;

        explicit ContainerModelBase(const scope::UsingContext &context) : TrackedObject(context) {
        }

        //
        // Used for lists and structures
        //
        virtual void rootsCheck(const ContainerModelBase *target) const = 0;
        virtual uint32_t size() const = 0;

        // allow overriding for containers where emptiness check is
        // faster than counting members (trees, graphs, etc)
        virtual bool empty() const {
            return size() == 0;
        }

        // Clone this container object (typically to make a read-only copy) - abstract version
        virtual std::shared_ptr<ContainerModelBase> clone() const = 0;

        void checkedPut(
            const StructElement &element,
            const std::function<void(const StructElement &)> &putAction);
        virtual std::shared_ptr<data::SharedBuffer> toJson();
        virtual std::shared_ptr<data::SharedBuffer> toYaml();
    };

    /**
     * Wraps a non-container value inside a container - almost equivalent of an array of
     * exactly one value.
     */
    class Boxed : public ContainerModelBase {
    protected:
        StructElement _value;
        mutable std::shared_mutex _mutex;
        void rootsCheck(const ContainerModelBase *target) const override;

    public:
        explicit Boxed(const scope::UsingContext &context) : ContainerModelBase(context) {
        }

        void put(const StructElement &element);
        StructElement get() const;
        uint32_t size() const override;
        void visit(Archive &archive) override;
        std::shared_ptr<ContainerModelBase> clone() const override;

        static std::shared_ptr<ContainerModelBase> box(
            const scope::UsingContext &context, const StructElement &element);
    };

    //
    // Base class for containers that behave like a structure - common between
    // shared structures and config
    //
    class StructModelBase : public ContainerModelBase {
    protected:
        virtual void putImpl(Symbol handle, const StructElement &element) = 0;
        virtual bool hasKeyImpl(Symbol handle) const = 0;
        virtual StructElement getImpl(Symbol handle) const = 0;

    public:
        explicit StructModelBase(const scope::UsingContext &context) : ContainerModelBase(context) {
        }

        void put(Symbol handle, const StructElement &element);
        void put(std::string_view sv, const StructElement &element);
        virtual std::vector<data::Symbol> getKeys() const = 0;
        virtual std::shared_ptr<ListModelBase> getKeysAsList() const = 0;
        bool hasKey(Symbol handle) const;
        bool hasKey(std::string_view sv) const;
        StructElement get(Symbol handle) const;
        StructElement get(std::string_view sv) const;
        void visit(Archive &archive) override;
        std::shared_ptr<ContainerModelBase> clone() const override;
        virtual std::shared_ptr<StructModelBase> copy() const = 0;
        virtual std::shared_ptr<StructModelBase> createForChild() = 0;
        [[nodiscard]] virtual Symbol foldKey(const Symbolish &key, bool ignoreCase) const = 0;
    };

    //
    // Base class for containers that behave like a list - common between shared
    // structures and config
    //
    class ListModelBase : public ContainerModelBase {
    public:
        explicit ListModelBase(const scope::UsingContext &context) : ContainerModelBase(context) {
        }

        virtual void put(int32_t idx, const StructElement &element) = 0;
        virtual void insert(int32_t idx, const StructElement &element) = 0;
        virtual StructElement get(int idx) const = 0;
        std::shared_ptr<ContainerModelBase> clone() const override;
        virtual std::shared_ptr<ListModelBase> copy() const = 0;
        void visit(Archive &archive) override;
    };

    class StructArchiver : public AbstractArchiver {
        std::shared_ptr<StructModelBase> _model;

    public:
        explicit StructArchiver(const std::shared_ptr<StructModelBase> &model) : _model(model) {
        }
        [[nodiscard]] bool canVisit() const override {
            return false;
        }
        [[nodiscard]] bool hasValue() const override {
            return true;
        }
        std::shared_ptr<ArchiveAdapter> key(const Symbol &symbol) override;
        [[nodiscard]] std::vector<Symbol> keys() const override;
        void visit(ValueType &vt) override;
    };

    class StructKeyArchiver : public AbstractArchiver {
        std::shared_ptr<StructModelBase> _model;
        Symbol _key;

    public:
        explicit StructKeyArchiver(
            const std::shared_ptr<StructModelBase> &model,
            const Symbol &key)
            : _model(model), _key(key) {
        }
        [[nodiscard]] bool canVisit() const override {
            return true;
        }
        [[nodiscard]] bool hasValue() const override;
        std::shared_ptr<ArchiveAdapter> key(const Symbol &symbol) override;
        std::shared_ptr<ArchiveAdapter> list() override;
        [[nodiscard]] bool isList() const noexcept override;
        [[nodiscard]] std::vector<Symbol> keys() const override;
        void visit(ValueType &vt) override;
    };

    class ElementDearchiver : public AbstractDearchiver {
        data::StructElement _element;

    public:
        explicit ElementDearchiver(data::StructElement element) : _element(std::move(element)) {
        }
        [[nodiscard]] bool canVisit() const override {
            return true;
        }

    protected:
        StructElement read() const override {
            return _element;
        }
    };

    /**
     * List archiver follows a slightly different pattern than struct in auto-appending
     */
    class ListArchiver : public AbstractArchiver {
        std::shared_ptr<ListModelBase> _list;
        int32_t _index{0};

    public:
        explicit ListArchiver(const std::shared_ptr<ListModelBase> &list) : _list(list) {
        }
        [[nodiscard]] bool canVisit() const override {
            return true;
        }
        void visit(ValueType &vt) override;
        std::shared_ptr<ArchiveAdapter> list() override;
        [[nodiscard]] bool isList() const noexcept override {
            return false;
        }
        [[nodiscard]] bool hasValue() const noexcept override {
            return true;
        }
        bool advance() noexcept override;
    };

    /**
     * List dearchiver follows a slightly different pattern than struct in auto-advancing
     */
    class ListDearchiver : public AbstractDearchiver {
        std::shared_ptr<ListModelBase> _list;
        int32_t _index{0};
        int32_t _size;

    public:
        explicit ListDearchiver(const std::shared_ptr<ListModelBase> &list)
            : _list(list), _size(util::safeBoundPositive<int32_t>(list->size())) {
        }
        [[nodiscard]] bool canVisit() const override;
        bool advance() noexcept override;

    protected:
        [[nodiscard]] StructElement read() const override;
    };

} // namespace data
