#pragma once
#include "data/value_type.hpp"
#include <filesystem>
#include <list>
#include <map>
#include <memory>
#include <unordered_map>
#include <util.hpp>
#include <vector>

namespace data {
    class Archive;
    class Serializable;
    class StructElement;
    class StructModelBase;
    class ContainerModelBase;

    /**
     * Base class for archiver or de-archiver
     */
    class ArchiveAdapter {
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
        virtual std::shared_ptr<ArchiveAdapter> key(const Symbol &symbol);
        /**
         * Visit as list
         * @return list style adapter
         */
        virtual std::shared_ptr<ArchiveAdapter> list();
        [[nodiscard]] virtual bool canVisit() const = 0;
        [[nodiscard]] virtual bool hasValue() const = 0;
        virtual void visit(data::ValueType &vt) = 0;
        virtual void visit(bool &) = 0;
        virtual void visit(int32_t &) = 0;
        virtual void visit(uint32_t &) = 0;
        virtual void visit(int64_t &) = 0;
        virtual void visit(uint64_t &) = 0;
        virtual void visit(float &) = 0;
        virtual void visit(double &) = 0;
        virtual void visit(std::string &) = 0;
        virtual void visit(Symbol &) = 0;
        virtual void visit(Archive &other) = 0;
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
        [[nodiscard]] virtual std::vector<Symbol> keys() const;
    };

    /**
     * Visitor pattern for representing a null entry (that fails)
     */
    class NullArchiveEntry : public ArchiveAdapter {
    private:
        template<typename T>
        void visitDefault(T &value, const T defValue) {
            if(!isArchiving()) {
                value = defValue;
            }
        }

    public:
        std::shared_ptr<ArchiveAdapter> key(const Symbol &symbol) override {
            return std::make_shared<NullArchiveEntry>();
        }

        std::shared_ptr<ArchiveAdapter> list() override {
            return std::make_shared<NullArchiveEntry>();
        }

        [[nodiscard]] bool canVisit() const override {
            return false;
        }

        [[nodiscard]] bool hasValue() const override {
            return false;
        }

        void visit(data::ValueType &vt) override {
            visitDefault(vt, data::ValueType{});
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
        void visit(Symbol &v) override {
            visitDefault(v, Symbol{});
        }
        void visit(Archive &) override {
        }
    };

    /**
     * Visitor pattern for something that will create an archive (base class)
     */
    class AbstractArchiver : public ArchiveAdapter {
    public:
        void visit(data::ValueType &vt) override = 0;

        void visit(bool &i) override {
            data::ValueType v{i};
            visit(v);
        }
        void visit(int32_t &i) override {
            data::ValueType v{i};
            visit(v);
        }
        void visit(uint32_t &i) override {
            data::ValueType v{i};
            visit(v);
        }
        void visit(int64_t &i) override {
            data::ValueType v{i};
            visit(v);
        }
        void visit(uint64_t &i) override {
            data::ValueType v{i};
            visit(v);
        }
        void visit(float &f) override {
            data::ValueType v{f};
            visit(v);
        }
        void visit(double &d) override {
            data::ValueType v{d};
            visit(v);
        }
        void visit(std::string &str) override {
            data::ValueType v{str};
            visit(v);
        }
        void visit(Symbol &symbol) override {
            data::ValueType v{symbol};
            visit(v);
        }
        void visit(Archive &) override;
        [[nodiscard]] bool isArchiving() const noexcept override {
            return true;
        }
    };

    /**
     * Visitor pattern for something that will initialize from an archive (base class)
     */
    class AbstractDearchiver : public ArchiveAdapter {

    protected:
        [[nodiscard]] virtual StructElement read() const = 0;

    public:
        void visit(data::ValueType &vt) override;
        void visit(bool &b) override;
        void visit(int32_t &i) override;
        void visit(uint32_t &i) override;
        void visit(int64_t &i) override;
        void visit(uint64_t &i) override;
        void visit(float &f) override;
        void visit(double &d) override;
        void visit(std::string &str) override;
        void visit(Symbol &symbol) override;
        void visit(Archive &) override;
        std::shared_ptr<ArchiveAdapter> key(const Symbol &symbol) override;
        std::shared_ptr<ArchiveAdapter> list() override;
        bool canVisit() const override;
        bool hasValue() const override;
        bool isList() const noexcept override;
        std::vector<Symbol> keys() const override;
    };

    class Archive {
        std::shared_ptr<ArchiveAdapter> _adapter;

        template<typename T>
        void visitListLike(T &value) {
            // Complex list-like scenario
            auto list = Archive(_adapter->list());
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
                    auto perKey = key(Symbolish{kv.first});
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

        template<typename T>
        T *initSharedPtr(std::shared_ptr<T> &) {
            return nullptr;
        }

    public:
        explicit Archive(const std::shared_ptr<ArchiveAdapter> &adapter) : _adapter(adapter) {
        }

        void setIgnoreCase(bool f = true) {
            _adapter->setIgnoreKeyCase(f);
        }

        bool isIgnoreCase() {
            return _adapter->isIgnoreCase();
        }

        bool isArchiving() {
            return _adapter->isArchiving();
        }

        bool hasValue() {
            return _adapter->hasValue();
        }

        explicit operator bool() {
            return hasValue();
        }

        bool operator!() {
            return !hasValue();
        }

        template<typename T>
        void operator()(const data::Symbolish &symbol, T &value) {
            key(symbol).visit(value);
        }

        template<typename T>
        void operator()(const data::Symbolish &symbol, T *pValue) {
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

        Archive operator[](const data::Symbolish &symbol) {
            return key(symbol);
        }

        Archive key(const data::Symbolish &symbol) {
            auto a = Archive(_adapter->key(symbol));
            return a;
        }

        std::vector<Symbol> keys() {
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
                v = initSharedPtr(value);
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
            if constexpr(std::is_base_of_v<Serializable, T>) {
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

        ArchiveAdapter *operator->() {
            return _adapter.get();
        }

        ArchiveAdapter &operator*() {
            return *_adapter;
        }

        static void readFromStruct(
            const std::shared_ptr<ContainerModelBase> &data, Serializable &target);
        static void readFromFile(const std::filesystem::path &file, Serializable &target);
        static void readFromYamlFile(const std::filesystem::path &file, Serializable &target);
        static void readFromJsonFile(const std::filesystem::path &file, Serializable &target);
        static void writeToStruct(
            const std::shared_ptr<StructModelBase> &data, Serializable &target);
        static void writeToFile(const std::filesystem::path &file, Serializable &source);
        static void writeToYamlFile(const std::filesystem::path &file, Serializable &source);
        static void writeToJsonFile(const std::filesystem::path &file, Serializable &source);
    };

    class Serializable {
    protected:

    public:
        Serializable() = default;
        virtual ~Serializable() = default;
        Serializable(const Serializable &other) = default;
        Serializable(Serializable &&) = default;
        Serializable &operator=(const Serializable &other) = default;
        Serializable &operator=(Serializable &&) = default;
        virtual void visit(Archive &archive) = 0;
    };

} // namespace data
