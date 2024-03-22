#pragma once
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <util.hpp>
#include <vector>

namespace util {

    template<typename Traits>
    class ArchiveBase;

    /**
     * Interface class implemented by structures that implement the archive visit pattern.
     */
    template<typename Traits>
    // NOLINTNEXTLINE(*-special-member-functions)
    class SerializableBase {
    public:
        virtual ~SerializableBase() noexcept = default;
        virtual void visit(ArchiveBase<Traits> &archive) = 0;
    };

    /**
     * Base class for archiver or de-archiver
     */
    template<typename Traits>
    class ArchiveAdapter {
    public:
        using ValueType = typename Traits::ValueType;
        using KeyType = typename Traits::SymbolType;
        using AutoKeyType = typename Traits::AutoKeyType;
        using SymbolType = typename Traits::SymbolType;
        using ReadType = typename Traits::ReadType;

    protected:
        bool _ignoreKeyCase = false;

    public:
        ArchiveAdapter() noexcept = default;
        virtual ~ArchiveAdapter() noexcept = default;
        ArchiveAdapter(const ArchiveAdapter &other) = delete;
        ArchiveAdapter(ArchiveAdapter &&) noexcept = default;
        ArchiveAdapter &operator=(const ArchiveAdapter &other) = delete;
        ArchiveAdapter &operator=(ArchiveAdapter &&) = delete;

        void setIgnoreKeyCase(bool ignoreCase = true) noexcept {
            _ignoreKeyCase = ignoreCase;
        }
        [[nodiscard]] bool isIgnoreCase() const noexcept {
            return _ignoreKeyCase;
        }
        /**
         * Visit a key - returned adapter changes value of the key
         * @param symbol ID of key
         * @return adapter for value
         */
        virtual std::shared_ptr<ArchiveAdapter> key(const KeyType &symbol) {
            throw std::runtime_error("Not a structure");
        }
        /**
         * Visit as list
         * @return list style adapter
         */
        virtual std::shared_ptr<ArchiveAdapter> list() {
            throw std::runtime_error("Not a list");
        }
        [[nodiscard]] virtual bool canVisit() const = 0;
        [[nodiscard]] virtual bool hasValue() const = 0;
        virtual void visit(ValueType &vt) = 0;
        virtual void visit(bool &) = 0;
        virtual void visit(int32_t &) = 0;
        virtual void visit(uint32_t &) = 0;
        virtual void visit(int64_t &) = 0;
        virtual void visit(uint64_t &) = 0;
        virtual void visit(float &) = 0;
        virtual void visit(double &) = 0;
        virtual void visit(std::string &) = 0;
        virtual void visit(KeyType &) = 0;
        virtual void visit(ArchiveBase<Traits> &other) = 0;
        /**
         * @return true if archiving, false if dearchiving
         */
        [[nodiscard]] virtual bool isArchiving() const noexcept {
            return false;
        }
        /**
         * @return true if list() can be called for list visitation
         */
        [[nodiscard]] virtual bool isList() const noexcept {
            return false;
        }
        /**
         * Call on a list adapter to advance element index
         */
        virtual bool advance() noexcept {
            return false;
        }
        [[nodiscard]] virtual std::vector<KeyType> keys() const {
            return {};
        }
    };

    /**
     * Visitor pattern for representing a null entry (that fails)
     */
    template<typename Traits>
    class NullArchiveEntry : public ArchiveAdapter<Traits> {
    public:
        using AdapterBase = ArchiveAdapter<Traits>;
        using KeyType = typename AdapterBase::KeyType;
        using AutoKeyType = typename AdapterBase::AutoKeyType;
        using SymbolType = typename AdapterBase::SymbolType;
        using ValueType = typename AdapterBase::ValueType;
        using ReadType = typename AdapterBase::ReadType;

    private:
        template<typename T>
        void visitDefault(T &value, const T defValue) {
            if(!ArchiveAdapter<Traits>::isArchiving()) {
                value = defValue;
            }
        }

    public:
        static std::shared_ptr<NullArchiveEntry> getNull() {
            static std::shared_ptr<NullArchiveEntry> nullArchiveEntry =
                std::make_shared<NullArchiveEntry>();
            return nullArchiveEntry;
        }

        std::shared_ptr<ArchiveAdapter<Traits>> key(const KeyType &) override {
            return getNull();
        }

        std::shared_ptr<ArchiveAdapter<Traits>> list() override {
            return getNull();
        }

        [[nodiscard]] bool canVisit() const override {
            return false;
        }

        [[nodiscard]] bool hasValue() const override {
            return false;
        }

        void visit(ValueType &vt) override {
            visitDefault(vt, ValueType{});
        }

        void visit(bool &v) override {
            visitDefault<bool>(v, false);
        }
        void visit(int32_t &v) override {
            visitDefault<int32_t>(v, 0);
        }
        void visit(uint32_t &v) override {
            visitDefault<uint32_t>(v, 0);
        }
        void visit(int64_t &v) override {
            visitDefault<int64_t>(v, 0);
        }
        void visit(uint64_t &v) override {
            visitDefault<uint64_t>(v, 0);
        }
        void visit(float &v) override {
            visitDefault(v, std::numeric_limits<float>::quiet_NaN());
        }
        void visit(double &v) override {
            visitDefault(v, std::numeric_limits<double>::quiet_NaN());
        }
        void visit(std::string &v) override {
            visitDefault(v, std::string{});
        }
        void visit(SymbolType &v) override {
            visitDefault(v, SymbolType{});
        }
        void visit(ArchiveBase<Traits> &) override {
        }
    };

    /**
     * Visitor pattern for something that will create an archive (base class)
     */
    template<typename Traits>
    class AbstractArchiver : public ArchiveAdapter<Traits> {
    public:
        using AdapterBase = ArchiveAdapter<Traits>;
        using KeyType = typename AdapterBase::KeyType;
        using AutoKeyType = typename AdapterBase::AutoKeyType;
        using SymbolType = typename AdapterBase::SymbolType;
        using ValueType = typename AdapterBase::ValueType;
        using ReadType = typename AdapterBase::ReadType;

        void visit(ValueType &vt) override = 0;

        void visit(bool &i) override {
            ValueType v{i};
            visit(v);
        }
        void visit(int32_t &i) override {
            ValueType v{i};
            visit(v);
        }
        void visit(uint32_t &i) override {
            ValueType v{i};
            visit(v);
        }
        void visit(int64_t &i) override {
            ValueType v{i};
            visit(v);
        }
        void visit(uint64_t &i) override {
            ValueType v{i};
            visit(v);
        }
        void visit(float &f) override {
            ValueType v{f};
            visit(v);
        }
        void visit(double &d) override {
            ValueType v{d};
            visit(v);
        }
        void visit(std::string &str) override {
            ValueType v{str};
            visit(v);
        }
        void visit(SymbolType &symbol) override {
            ValueType v{symbol};
            visit(v);
        }
        void visit(ArchiveBase<Traits> &other) override {
            if(this->canVisit() && other->canVisit()) {
                ValueType v;
                other.visit(v);
                visit(v);
            }

            auto keySet = other.keys();
            for(auto &k : keySet) {
                auto me = this->key(k);
                auto otherKey = other.key(k);
                me->visit(otherKey);
            }
        }
        [[nodiscard]] bool isArchiving() const noexcept override {
            return true;
        }
    };

    /**
     * Visitor pattern for something that will initialize from an archive (base class)
     */
    template<typename Traits>
    class AbstractDearchiver : public ArchiveAdapter<Traits> {

    public:
        using AdapterBase = ArchiveAdapter<Traits>;
        using KeyType = typename AdapterBase::KeyType;
        using AutoKeyType = typename AdapterBase::AutoKeyType;
        using SymbolType = typename AdapterBase::SymbolType;
        using ValueType = typename AdapterBase::ValueType;
        using ReadType = typename AdapterBase::ReadType;

    protected:
        [[nodiscard]] virtual ReadType read() const = 0;

    public:
        void visit(ValueType &vt) override {
            auto rv = read();
            vt = Traits::toValue(rv);
        }
        void visit(bool &b) override {
            auto rv = read();
            b = Traits::toBool(rv);
        }
        void visit(int32_t &i) override {
            auto rv = read();
            i = static_cast<int32_t>(Traits::toInt64(rv));
        }
        void visit(uint32_t &i) override {
            auto rv = read();
            i = static_cast<uint32_t>(Traits::toInt64(rv));
        }
        void visit(int64_t &i) override {
            auto rv = read();
            i = static_cast<int64_t>(Traits::toInt64(rv));
        }
        void visit(uint64_t &i) override {
            auto rv = read();
            i = static_cast<uint64_t>(Traits::toInt64(rv));
        }
        void visit(float &f) override {
            auto rv = read();
            f = static_cast<float>(Traits::toDouble(rv));
        }
        void visit(double &d) override {
            auto rv = read();
            d = Traits::toDouble(rv);
        }
        void visit(std::string &str) override {
            auto rv = read();
            str = Traits::toString(rv);
        }
        void visit(SymbolType &symbol) override {
            auto rv = read();
            symbol = Traits::toSymbol(rv);
        }
        [[nodiscard]] std::shared_ptr<AdapterBase> key(const KeyType &key) override {
            auto rv = read();
            return Traits::toKey(rv, key, this->isIgnoreCase());
        }
        [[nodiscard]] std::vector<KeyType> keys() const override {
            auto rv = read();
            return Traits::toKeys(rv);
        }
        [[nodiscard]] std::shared_ptr<AdapterBase> list() override {
            auto rv = read();
            return Traits::toList(rv);
        }
        [[nodiscard]] bool canVisit() const override {
            return true;
        }
        [[nodiscard]] bool hasValue() const override {
            auto rv = read();
            return Traits::hasValue(rv);
        }
        [[nodiscard]] bool isList() const noexcept override {
            auto rv = read();
            return Traits::isList(rv);
        }
        void visit(ArchiveBase<Traits> &other) override {
            if(isList() || other->isList()) {
                // List visitor case
                auto me = list();
                auto otherList = other->list();
                while(me->canVisit() && otherList->canVisit()) {
                    ValueType v;
                    me->visit(v); // retrieve value
                    otherList->visit(v); // write value
                    me->advance();
                    otherList->advance();
                }
            } else if(canVisit() && other->canVisit()) {
                // Scalar visitor case
                ValueType v;
                visit(v); // retrieve value
                other.visit(v); // write value
            }

            // Subkeys
            auto keySet = keys();
            for(auto &k : keySet) {
                auto me = key(k);
                auto otherKey = other.key(k);
                me->visit(otherKey);
            }
        }
    };

    template<typename Traits>
    class ArchiveBase {
    public:
        using SerializableType = typename Traits::SerializableType;
        using AdapterBase = ArchiveAdapter<Traits>;
        using KeyType = typename AdapterBase::KeyType;
        using AutoKeyType = typename AdapterBase::AutoKeyType;
        using SymbolType = typename AdapterBase::SymbolType;
        using ValueType = typename AdapterBase::ValueType;

    private:
        std::shared_ptr<AdapterBase> _adapter;

        template<typename T>
        void visitListLike(T &value) {
            // Complex list-like scenario
            auto list = ArchiveBase(_adapter->list());
            if(list.isArchiving()) {
                for(auto &v : value) {
                    list.visit(v);
                    list->advance();
                }
            } else {
                value.clear();
                while(list->canVisit()) {
                    typename T::value_type v{};
                    list.visit(v);
                    value.push_back(std::move(v));
                    list->advance();
                }
            }
        }

        template<typename T>
        void visitMapLike(T &value) {
            // Complex map-like scenario
            if(isArchiving()) {
                // get keys from value
                for(auto &kv : value) {
                    auto perKey = key(AutoKeyType{kv.first});
                    perKey.visit(kv.second);
                }
            } else {
                // get keys from adapter
                auto myKeys = keys();
                for(const auto &k : myKeys) {
                    auto perKey = key(k); // archive entry
                    auto destKey = typename T::key_type{k}; // key for dest map
                    typename T::mapped_type destVal; // value for dest map
                    perKey.visit(destVal);
                    value.emplace(std::move(destKey), std::move(destVal));
                }
            }
        }

    public:
        explicit ArchiveBase(const std::shared_ptr<AdapterBase> &adapter) : _adapter(adapter) {
        }

        void setIgnoreCase(bool f = true) {
            _adapter->setIgnoreKeyCase(f);
        }

        [[nodiscard]] bool isIgnoreCase() const {
            return _adapter->isIgnoreCase();
        }

        [[nodiscard]] bool isArchiving() const {
            return _adapter->isArchiving();
        }

        [[nodiscard]] bool hasValue() const {
            return _adapter->hasValue();
        }

        [[nodiscard]] explicit operator bool() const {
            return hasValue();
        }

        [[nodiscard]] bool operator!() const {
            return !hasValue();
        }

        template<typename T>
        void operator()(const AutoKeyType &symbol, T &value) {
            key(symbol).visit(value);
        }

        template<typename T>
        void operator()(const AutoKeyType &symbol, T *pValue) {
            key(symbol).visit(pValue);
        }

        template<typename T>
        void operator()(T &value) {
            visit(value);
        }

        template<typename T>
        void operator()(T *pValue) {
            visit(pValue);
        }

        [[nodiscard]] ArchiveBase operator[](const AutoKeyType &symbol) {
            return key(symbol);
        }

        [[nodiscard]] ArchiveBase key(const AutoKeyType &symbol) {
            auto a = ArchiveBase(_adapter->key(symbol));
            return a;
        }

        [[nodiscard]] std::vector<KeyType> keys() {
            return _adapter->keys();
        }

        template<typename T>
        void visit(std::list<T> &value) {
            visitListLike(value);
        }

        template<typename T>
        void visit(std::vector<T> &value) {
            visitListLike(value);
        }

        template<typename K, typename V>
        void visit(std::map<K, V> &value) {
            visitMapLike(value);
        }

        template<typename K, typename V>
        void visit(std::unordered_map<K, V> &value) {
            visitMapLike(value);
        }

        template<typename T>
        void visit(std::shared_ptr<T> &value) {
            auto v = value.get();
            if(!v && !isArchiving() && hasValue()) {
                v = Traits::template initSharedPtr<T>(value);
            }
            visit(v);
        }

        template<typename T>
        void visit(std::optional<T> &ov) {
            if(isArchiving()) {
                if(ov.has_value()) {
                    visit(ov.value());
                }
            } else if(hasValue()) {
                T v;
                visit(v);
                ov.emplace(std::move(v));
            } else {
                ov.reset();
            }
        }

        template<typename T>
        void visit(T &value) {
            if constexpr(std::is_base_of_v<SerializableType, T>) {
                // Delegate
                value.visit(*this);
            } else {
                // Trivial types
                _adapter->visit(value);
            }
        }

        template<typename T>
        void visit(T *pValue) {
            if(pValue) {
                visit(*pValue);
            }
        }

        [[nodiscard]] AdapterBase *operator->() {
            return _adapter.get();
        }

        [[nodiscard]] AdapterBase &operator*() {
            return *_adapter;
        }
    };

} // namespace util
