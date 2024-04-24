#pragma once
#include "api_errors.hpp"
#include "api_forwards.hpp"
#include "archive.hpp"
#include "containers.hpp"

namespace ggapi {
    struct ArchiveTraits {
        using SerializableType = Serializable;
        using ValueType = Container;
        using ReadType = Container;
        using SymbolType = Symbol;
        using KeyType = SymbolType;
        using AutoKeyType = SymbolType;

        static ValueType toValue(const ReadType &rv) {
            return rv;
        }

        template<typename T>
        static ValueType valueOf(const T &in) {
            return Container::box(in);
        }

        template<typename T>
        static T toScalar(const ReadType &obj, const T &def) {
            if(!obj) {
                return def;
            }
            if(obj.isScalar()) {
                return obj.unbox<T>();
            } else {
                throw std::runtime_error("Type mismatch");
            }
        }

        static ObjHandle unbox(const ReadType &obj) {
            return obj.unbox<ObjHandle>();
        }

        static SymbolType toSymbol(const ReadType &rv) {
            return toScalar<Symbol>(rv, {});
        }
        static std::string toString(const ReadType &rv) {
            return toScalar<std::string>(rv, "");
        }
        static uint64_t toInt64(const ReadType &rv) {
            return toScalar<uint64_t>(rv, 0);
        }
        static double toDouble(const ReadType &rv) {
            return toScalar<double>(rv, 0.0);
        }
        static bool toBool(const ReadType &rv) {
            return toScalar<bool>(rv, false);
        }
        static bool hasValue(const ReadType &rv) {
            return !!rv;
        }
        static bool isList(const ReadType &rv) {
            return rv.isList(); // assume no need to unbox
        }
        static bool isStruct(const ReadType &rv) {
            return rv.isStruct(); // assume no need to unbox
        }
        static std::shared_ptr<ArchiveAdapter> toKey(
            const ArchiveTraits::ReadType &rv, const ArchiveTraits::KeyType &key, bool ignoreCase);
        static std::shared_ptr<ArchiveAdapter> toList(const ReadType &rv);
        static std::vector<KeyType> toKeys(const ReadType &rv);
        template<typename T>
        static T *initSharedPtr(std::shared_ptr<T> &v) {
            v = std::make_shared<T>();
            return v.get();
        }

        template<typename T>
        static void visit(Archive &archive, T &value) {
            archive->visit(value);
        }

        static void visit(Archive &archive, Struct &data);
        static void visit(Archive &archive, List &data);
    };

    /**
     * Very generic (not necessarily efficient) dearchiver taking advantage of the boxing capability
     * to abstract out object types
     */
    class ContainerDearchiver : public AbstractDearchiver {
        const Container _element;

    public:
        explicit ContainerDearchiver(Container element) : _element(std::move(element)) {
        }
        [[nodiscard]] bool canVisit() const override {
            return true;
        }

    protected:
        [[nodiscard]] Container read() const override {
            return _element;
        }
    };

    /**
     * List dearchiver - specializes in ability to read values of list sequentially
     */
    class ListDearchiver : public AbstractDearchiver {
        const List _list;
        int32_t _index{0};
        int32_t _size;

    public:
        explicit ListDearchiver(List list)
            : _list(std::move(list)), _size(util::safeBoundPositive<int32_t>(_list.size())) {
        }

        [[nodiscard]] bool canVisit() const override {
            return _index < _size;
        }

        [[nodiscard]] bool advance() noexcept override {
            if(canVisit()) {
                ++_index;
                return canVisit();
            } else {
                return false;
            }
        }

    protected:
        [[nodiscard]] Container read() const override {
            if(canVisit()) {
                return _list.get<Container>(_index);
            } else {
                return {};
            }
        }
    };

    inline std::shared_ptr<ArchiveAdapter> ArchiveTraits::toKey(
        const ReadType &rv, const KeyType &key, bool ignoreCase) {

        if(isStruct(rv)) {
            auto refStruct = Struct{rv};
            auto refKey = key;
            if(ignoreCase) {
                refKey = refStruct.foldKey(key);
            }
            return std::make_shared<ContainerDearchiver>(
                Container::box(refStruct.get<ObjHandle>(refKey)));
        } else if(hasValue(rv)) {
            throw std::runtime_error("Not a Struct container");
        } else {
            return util::NullArchiveEntry<ArchiveTraits>::getNull();
        }
    }

    inline std::shared_ptr<ArchiveAdapter> ArchiveTraits::toList(const ReadType &rv) {
        if(isList(rv)) {
            return std::make_shared<ListDearchiver>(List{rv});
        } else if(hasValue(rv)) {
            throw std::runtime_error("Not a List container");
        } else {
            return util::NullArchiveEntry<ArchiveTraits>::getNull();
        }
    }

    inline std::vector<ArchiveTraits::KeyType> ArchiveTraits::toKeys(const ReadType &rv) {
        if(isStruct(rv)) {
            return Struct{rv}.keys().toVector<ArchiveTraits::KeyType>();
        } else {
            return {};
        }
    }

    /**
     * List archiver follows a slightly different pattern than struct in auto-appending
     */
    class ListArchiver : public AbstractArchiver {
        List _list;
        int32_t _index{0};

    public:
        explicit ListArchiver(List list) : _list(std::move(list)) {
        }
        [[nodiscard]] bool canVisit() const override {
            return true;
        }
        void visit(ValueType &vt) override {
            _list.put(_index, vt);
        }
        std::shared_ptr<ArchiveAdapter> key(const KeyType &key) override;
        std::shared_ptr<ArchiveAdapter> list() override {
            auto entry = List::create();
            _list.put(_index, entry);
            return std::make_shared<ListArchiver>(entry);
        }
        [[nodiscard]] bool isList() const noexcept override {
            return false;
        }
        [[nodiscard]] bool hasValue() const noexcept override {
            return true;
        }
        bool advance() noexcept override {
            ++_index;
            return true;
        }
    };

    /**
     * Archiver to modify an individual key of a structure
     */
    class StructKeyArchiver : public AbstractArchiver {
        Struct _model;
        Symbol _key; // normalized

    public:
        explicit StructKeyArchiver(Struct model, Symbol key) : _model(std::move(model)), _key(key) {
        }
        [[nodiscard]] bool canVisit() const override {
            return true;
        }
        [[nodiscard]] bool hasValue() const override {
            return _model.hasKey(_key);
        }
        std::shared_ptr<ArchiveAdapter> key(const Symbol &subKey) override {
            auto refStruct = _model.get<Struct>(_key);
            if(!refStruct) {
                refStruct = _model.createForChild();
                _model.put(_key, refStruct);
            }
            Symbol refKey = subKey;
            if(isIgnoreCase()) {
                refKey = refStruct.foldKey(subKey);
            }
            return std::make_shared<StructKeyArchiver>(refStruct, refKey);
        }
        [[nodiscard]] std::vector<Symbol> keys() const override {
            auto e = _model.get<Container>(_key);
            return ArchiveTraits::toKeys(e);
        }
        std::shared_ptr<ArchiveAdapter> list() override {
            auto refList = _model.get<List>(_key);
            if(!refList) {
                refList = List::create();
                _model.put(_key, refList);
            }
            return std::make_shared<ListArchiver>(refList);
        }
        [[nodiscard]] bool isList() const noexcept override {
            try {
                auto e = _model.get<Container>(_key);
                return ArchiveTraits::isList(e);
            } catch(...) {
                return false;
            }
        }
        void visit(ValueType &vt) override {
            _model.put(_key, vt);
        }
    };

    inline std::shared_ptr<ArchiveAdapter> ListArchiver::key(const ArchiveAdapter::KeyType &key) {
        auto refStruct = _list.get<Struct>(_index);
        if(!refStruct) {
            refStruct = Struct::create();
            _list.put(_index, refStruct);
        }
        Symbol refKey = key;
        if(isIgnoreCase()) {
            refKey = refStruct.foldKey(key);
        }
        return std::make_shared<StructKeyArchiver>(refStruct, refKey);
    }

    /**
     * Archiver to modify a structure - for the most part, this is responsible for key and keys
     */
    class StructArchiver : public AbstractArchiver {
        Struct _model;

    public:
        explicit StructArchiver(Struct model) : _model(std::move(model)) {
        }
        [[nodiscard]] bool canVisit() const override {
            return false;
        }
        [[nodiscard]] bool hasValue() const override {
            return true;
        }
        std::shared_ptr<ArchiveAdapter> key(const Symbol &key) override {
            Symbol refKey = key;
            if(isIgnoreCase()) {
                refKey = _model.foldKey(key);
            }
            return std::make_shared<StructKeyArchiver>(_model, refKey);
        }

        [[nodiscard]] std::vector<Symbol> keys() const override {
            return ArchiveTraits::toKeys(_model);
        }

        void visit(ValueType &vt) override {
            throw std::runtime_error("Unsupported visit");
        }
    };

    inline void ArchiveTraits::visit(ggapi::Archive &archive, ggapi::Struct &data) {
        if(archive.isArchiving()) {
            // data is source
            if(data) {
                Archive other = Archive(std::make_shared<ContainerDearchiver>(data));
                archive.visit(other);
            } else {
                Archive other = Archive(util::NullArchiveEntry<ArchiveTraits>::getNull());
                archive.visit(other);
            }
        } else {
            // data is target
            if(!data) {
                data = ggapi::Struct::create();
            }
            Archive other = Archive(std::make_shared<StructArchiver>(data));
            archive.visit(other);
        }
    }

    inline void ArchiveTraits::visit(ggapi::Archive &archive, ggapi::List &data) {
        if(archive.isArchiving()) {
            // data is source
            if(data) {
                Archive other = Archive(std::make_shared<ContainerDearchiver>(data));
                archive.visit(other);
            } else {
                Archive other = Archive(util::NullArchiveEntry<ArchiveTraits>::getNull());
                archive.visit(other);
            }
        } else {
            // data is target
            if(!data) {
                data = ggapi::List::create();
            }
            Archive other = Archive(std::make_shared<ListArchiver>(data));
            archive.visit(other);
        }
    }

    /**
     * Translate a dynamic data structure to a C++ structure with validation
     * @param data Dynamic structure
     * @param target C++ structure (must implement Serializable)
     */
    inline void deserialize(const Container &data, Serializable &target) {
        auto archive = Archive(std::make_shared<ContainerDearchiver>(data));
        archive.visit(target);
    }

    /**
     * Translate a C++ structure to a dynamic structure
     * @param target C++ structure (must implement Serializable)
     * @return dynamic structure
     */
    inline Struct serialize(Serializable &target) {
        auto data = Struct::create();
        auto archive = Archive(std::make_shared<StructArchiver>(data));
        archive.visit(target);
        return data;
    }

} // namespace ggapi
