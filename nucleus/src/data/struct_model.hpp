#pragma once
#include "handle_table.hpp"
#include "string_table.hpp"
#include <map>
#include <mutex>
#include <set>
#include <variant>
#include <vector>

namespace data {
    class ContainerModelBase;
    class StructModelBase;
    class ListModelBase;

    typedef std::variant<
        // types in same order as type consts in ValueTypes below
        std::monostate, // Always first (NONE)
        bool, // BOOL
        uint64_t, // INT
        double, // DOUBLE
        std::string, // STRING
        std::shared_ptr<TrackedObject> // OBJECT
        >
        ValueType;

    // enum class ValueTypes would seem to be better, but we need to compare int to
    // int, so this is easier
    struct ValueTypes {
        static constexpr auto NONE{0};
        static constexpr auto BOOL{1};
        static constexpr auto INT{2};
        static constexpr auto DOUBLE{3};
        static constexpr auto STRING{4};
        static constexpr auto OBJECT{5};
    };

    //
    // Data storage element with implicit type conversion
    //
    class StructElement : public ValueTypes {
        friend class StructModelBase;

    protected:
        // size of variant is prob ~ 32 bytes or so, because of std::string
        // in theory, can be optimized, but is it worth it?
        ValueType _value;

    public:
        StructElement() : _value{} = default;

        StructElement(ValueType v) : _value(std::move(v)) { // NOLINT(*-explicit-constructor)
        }

        StructElement(const bool v) : _value{v} { // NOLINT(*-explicit-constructor)
        }

        StructElement(const uint64_t v) : _value{v} { // NOLINT(*-explicit-constructor)
        }

        StructElement(const double v) : _value{v} { // NOLINT(*-explicit-constructor)
        }

        StructElement(const std::shared_ptr<TrackedObject> &p); // NOLINT(*-explicit-constructor)

        // NOLINTNEXTLINE(*-explicit-constructor)
        StructElement(const std::shared_ptr<ContainerModelBase> &p)
            : StructElement(std::static_pointer_cast<TrackedObject>(p)) {
        }

        StructElement(const std::string &str) : _value{str} { // NOLINT(*-explicit-constructor)
        }

        StructElement(const StructElement &) = default;
        StructElement(StructElement &&) = default;
        StructElement &operator=(const StructElement &el) = default;
        StructElement &operator=(StructElement &&el) noexcept = default;
        virtual ~StructElement() = default;

        [[nodiscard]] virtual bool empty() const {
            // Note, we don't do implicit operators for this to avoid confusion with bool
            return _value.index() == NONE;
        }

        ValueType get() {
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

        [[nodiscard]] static bool isContainer() {
            return isType<ContainerModelBase>();
        }

        [[nodiscard]] bool isScalar() const {
            return !isObject();
        }

        [[nodiscard]] bool isNull() const {
            return _value.index() == NONE;
        }

        [[nodiscard]] static bool getBool() {
            switch(_value.index()) {
            case BOOL:
                return std::get<bool>(_value);
            case INT:
                return std::get<uint64_t>(_value) != 0;
            case DOUBLE:
                return std::get<double>(_value) != 0.0;
            case STRING: {
                std::string s = util::lower(std::get<std::string>(_value));
                return !s.empty() && s != "false" && s != "no" && s != "0" && s != "0.0";
            }
            default:
                throw std::runtime_error("Unsupported type conversion to integer");
            }
        }

        [[nodiscard]] static uint64_t getInt() {
            switch(_value.index()) {
            case BOOL:
                return std::get<bool>(_value) ? 1 : 0;
            case INT:
                return std::get<uint64_t>(_value);
            case DOUBLE:
                return static_cast<uint64_t>(std::get<double>(_value));
            case STRING:
                return std::stoul(std::get<std::string>(_value));
            default:
                throw std::runtime_error("Unsupported type conversion to integer");
            }
        }

        [[nodiscard]] static double getDouble() {
            switch(_value.index()) {
            case BOOL:
                return std::get<bool>(_value) ? 1.0 : 0.0;
            case INT:
                return static_cast<double>(std::get<uint64_t>(_value));
            case DOUBLE:
                return std::get<double>(_value);
            case STRING:
                return std::stod(std::get<std::string>(_value));
            default:
                throw std::runtime_error("Unsupported type conversion to double");
            }
        }

        [[nodiscard]] static std::string getString() {
            switch(_value.index()) {
            case BOOL:
                return std::get<bool>(_value) ? "true" : "false";
            case INT:
                return std::to_string(std::get<uint64_t>(_value));
            case DOUBLE:
                return std::to_string(std::get<double>(_value));
            case STRING:
                return std::get<std::string>(_value);
            default:
                throw std::runtime_error("Unsupported type conversion to string");
            }
        }

        [[nodiscard]] static std::shared_ptr<TrackedObject> getObject() {
            switch(_value.index()) {
            case NONE:
                return {};
            case OBJECT:
                return std::get<std::shared_ptr<TrackedObject>>(_value);
            default:
                throw std::runtime_error("Unsupported type conversion to object");
            }
        }

        [[nodiscard]] static std::shared_ptr<ContainerModelBase> getContainer() {
            return castObject<ContainerModelBase>();
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
    class ContainerModelBase : public TrackedObject {
    public:
        explicit ContainerModelBase(Environment &environment) : TrackedObject{environment} {
        }

        //
        // Used for lists and structures
        //
        virtual void rootsCheck(const ContainerModelBase *target) const = 0;
        [[nodiscard]] virtual uint32_t size() const = 0;
        void checkedPut(
            const StructElement &element,
            const std::function<void(const StructElement &)> &putAction
        );
    };

    //
    // Base class for containers that behave like a structure - common between
    // shared structures and config
    //
    class StructModelBase : public ContainerModelBase {
    protected:
        virtual void putImpl(StringOrd handle, const StructElement &element) = 0;
        [[nodiscard]] virtual bool hasKeyImpl(StringOrd handle) const = 0;
        [[nodiscard]] virtual StructElement getImpl(StringOrd handle) const = 0;

    public:
        explicit StructModelBase(Environment &environment) : ContainerModelBase{environment} {
        }

        void put(StringOrd handle, const StructElement &element);
        void put(std::string_view sv, const StructElement &element);
        [[nodiscard]] virtual std::vector<data::StringOrd> getKeys() const = 0;
        [[nodiscard]] bool hasKey(StringOrd handle) const;
        [[nodiscard]] bool hasKey(std::string_view sv) const;
        [[nodiscard]] StructElement get(StringOrd handle) const;
        [[nodiscard]] StructElement get(std::string_view sv) const;
        [[nodiscard]] virtual std::shared_ptr<StructModelBase> copy() const = 0;
    };

    //
    // Base class for containers that behave like a list - common between shared
    // structures and config
    //
    class ListModelBase : public ContainerModelBase {
    public:
        explicit ListModelBase(Environment &environment) : ContainerModelBase{environment} {
        }

        virtual void put(int32_t idx, const StructElement &element) = 0;
        virtual void insert(int32_t idx, const StructElement &element) = 0;
        [[nodiscard]] virtual StructElement get(int idx) const = 0;
        [[nodiscard]] virtual std::shared_ptr<ListModelBase> copy() const = 0;
    };

    inline StructElement::StructElement(const std::shared_ptr<TrackedObject> &p) {
        if(p) {
            _value = p;
        }
        // !p results in type NONE, force consistent treatment of null
    }

} // namespace data
