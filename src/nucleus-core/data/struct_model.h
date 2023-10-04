#pragma once
#include "handle_table.h"
#include "string_table.h"
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
        std::shared_ptr<ContainerModelBase> // CONTAINER
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
        static constexpr auto CONTAINER{5};
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
        StructElement() : _value{} {
        }

        explicit StructElement(ValueType v) : _value(std::move(v)) {
        }

        explicit StructElement(const uint64_t v) : _value{v} {
        }

        explicit StructElement(const double v) : _value{v} {
        }

        explicit StructElement(std::shared_ptr<ContainerModelBase> p);

        explicit StructElement(const std::string &str) : _value{str} {
        }

        StructElement(const StructElement &) = default;
        StructElement(StructElement &&) = default;
        StructElement &operator=(const StructElement &) = default;
        StructElement &operator=(StructElement &&) = default;
        ~StructElement() = default;

        explicit operator bool() const {
            return _value.index() != NONE;
        }

        bool operator!() const {
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

        [[nodiscard]] bool isContainer() const {
            return _value.index() == CONTAINER;
        }

        [[nodiscard]] bool isScalar() const {
            return !isContainer();
        }

        [[nodiscard]] bool getBool() const {
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

        [[nodiscard]] uint64_t getInt() const {
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

        [[nodiscard]] double getDouble() const {
            switch (_value.index()) {
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

        [[nodiscard]] std::string getString() const {
            switch (_value.index()) {
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

        [[nodiscard]] std::shared_ptr<ContainerModelBase> getContainer() const {
            switch(_value.index()) {
            case CONTAINER:
                return std::get<std::shared_ptr<ContainerModelBase>>(_value);
            default:
                throw std::runtime_error("Unsupported type conversion to object");
            }
        }

        template<typename T>
        [[nodiscard]] bool isType() const {
            static_assert(std::is_base_of_v<ContainerModelBase, T>);
            std::shared_ptr<T> p = std::dynamic_pointer_cast<T>(getContainer());
            return static_cast<bool>(p);
        }

        template<typename T>
        [[nodiscard]] std::shared_ptr<T> castContainer() const {
            static_assert(std::is_base_of_v<ContainerModelBase, T>);
            std::shared_ptr<T> p = std::dynamic_pointer_cast<T>(getContainer());
            if(p) {
                return p;
            }
            throw std::bad_cast();
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
        virtual uint32_t size() const = 0;
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
    public:
        explicit StructModelBase(Environment &environment) : ContainerModelBase{environment} {
        }

        virtual void put(StringOrd handle, const StructElement &element) = 0;

        virtual void put(std::string_view sv, const StructElement &element) = 0;

        virtual std::vector<data::StringOrd> getKeys() const = 0;

        virtual bool hasKey(StringOrd handle) const = 0;
        virtual StructElement get(StringOrd handle) const = 0;
        virtual StructElement get(std::string_view sv) const = 0;
        virtual std::shared_ptr<StructModelBase> copy() const = 0;
    };

    //
    // Base class for containers that behave like a list - common between shared
    // structures and config
    //
    class ListModelBase : public ContainerModelBase {
    public:
        explicit ListModelBase(Environment &environment) : ContainerModelBase{environment} {
        }

        virtual void put(int32_t idx, const StructElement & element) = 0;
        virtual void insert(int32_t idx, const StructElement & element) = 0;
        virtual StructElement get(int idx) const = 0;
        virtual std::shared_ptr<ListModelBase> copy() const = 0;
    };

    inline StructElement::StructElement(std::shared_ptr<ContainerModelBase> p) : _value{p} {
    }

} // namespace data
