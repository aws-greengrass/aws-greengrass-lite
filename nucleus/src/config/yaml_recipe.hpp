#pragma once
#include "conv/yaml_conv.hpp"
#include "data/serializable.hpp"
#include "scope/context_full.hpp"
#include "util.hpp"

namespace config {
    using IteratorType = YAML::const_iterator;

    template<class T>
    struct Field {
        bool _ignoreCase = false;
        std::string _key;
        T _value;

    public:
        Field(T &value) : _value(value) {
        }

        [[nodiscard]] std::string getKey() const {
            return _key;
        }

        T getDefaultValue() const {
            return T();
        }

        template<typename E = T>
        typename std::enable_if_t<std::is_enum_v<E>> fromString(const std::string &val) {
        }

        T getValue() const {
            return _value;
        }
    };

    // For now, essentially a rename
    class Object final : public data::StructElement {
    public:
        Object(const Object &) = default;
        Object(Object &&) = default;
        Object &operator=(const Object &el) = default;
        Object &operator=(Object &&el) noexcept = default;
        ~Object() final = default;

        Object() : data::StructElement() {
        }

        template<typename T>
        // NOLINTNEXTLINE(*-explicit-constructor)
        Object(const T &v) : data::StructElement(v) {
        }
    };

    class Iterator {
        size_t _itSize;
        bool _ignoreKeyCase = false;

    protected:
        size_t _itIndex{0};
        IteratorType _begin;
        IteratorType _end;
        IteratorType _current;

    public:
        virtual ~Iterator() = default;
        Iterator(const Iterator &other) = delete;
        Iterator(Iterator &&) = default;
        Iterator &operator=(const Iterator &other) = default;
        Iterator &operator=(Iterator &&) = default;
        explicit Iterator(const IteratorType &begin, const IteratorType &end)
            : _begin(begin), _current(begin), _end(end), _itSize(std::distance(begin, end)) {
        }

        [[nodiscard]] size_t size() const {
            return _itSize;
        }

        IteratorType begin() {
            return _begin;
        }

        IteratorType end() {
            return _end;
        }

        void operator++() {
            _itIndex++;
            ;
        }

        void traverse() {
            if(_itIndex >= size()) {
                throw std::runtime_error("No more items in the container"); // no more items
            }
            size_t idx = _itIndex;
            _current = _begin;
            while(idx > 0) {
                _current++;
                idx--;
            }
        }

        void setIgnoreKeyCase(bool ignoreCase = true) {
            _ignoreKeyCase = ignoreCase;
        }

        virtual IteratorType next() = 0;

        virtual YAML::Node find(const std::string &name) = 0;

        [[nodiscard]] virtual std::string name() = 0;

        [[nodiscard]] virtual YAML::Node value() = 0;

        [[nodiscard]] inline bool compareKeys(std::string key, std::string name) const {
            if(_ignoreKeyCase) {
                key = util::lower(key);
                name = util::lower(name);
            }
            return key == name;
        }
    };

    class MapIterator : public Iterator {

    public:
        explicit MapIterator(const IteratorType &begin, const IteratorType &end)
            : Iterator(begin, end) {
        }

        IteratorType next() override {
            // return the next node
            if(_itIndex >= size()) {
                return {}; // optional map
            }
            traverse();
            return _current;
        }

        YAML::Node find(const std::string &name) override {
            // do not care about index
            for(auto it = _begin; it != _end; it++) {
                auto key = it->first.as<std::string>();
                if(compareKeys(key, name)) {
                    return it->second;
                }
            }
            return {};
        }

        [[nodiscard]] std::string name() override {
            if(_itIndex >= size()) {
                return {}; // optional map
            }
            traverse();
            return _current->first.as<std::string>();
        }

        [[nodiscard]] YAML::Node value() override {
            if(_itIndex >= size()) {
                return {}; // optional map
            }
            traverse();
            return _current->second;
        }
    };

    class SequenceIterator : public Iterator {
        std::vector<IteratorType> _stack;

    public:
        explicit SequenceIterator(const IteratorType &begin, const IteratorType &end)
            : Iterator(begin, end) {
        }

        IteratorType next() override {
            // return the next node
            if(_itIndex >= size()) {
                return {}; // optional sequence
            }
            traverse();
            return _current;
        }

        YAML::Node find(const std::string &name) override {
            if(_itIndex >= size()) {
                return {}; // optional sequence
            }
            traverse();
            auto node = *_current;
            for(auto it = node.begin(); it != node.end(); it++) {
                auto key = it->first.as<std::string>();
                if(compareKeys(key, name)) {
                    return it->second;
                }
            }
            return {};
        }

        [[nodiscard]] std::string name() override {
            if(_itIndex >= size()) {
                return {}; // // optional sequence
            }
            traverse();
            auto node = *_current;
            return node.as<std::string>();
        }

        [[nodiscard]] YAML::Node value() override {
            if(_itIndex >= size()) {
                return {}; // optional sequence
            }
            traverse();
            return *_current;
        }
    };

    class YamlRecipeReader : public conv::Archive, private scope::UsesContext {
        std::vector<std::unique_ptr<Iterator>> _stack;

    public:
        explicit YamlRecipeReader(const scope::UsingContext &context)
            : scope::UsesContext{context} {
        }

        template<typename T>
        inline YamlRecipeReader &operator()(T &&arg) {
            process(std::forward<T>(arg));
            return *this;
        }

        template<typename T>
        inline YamlRecipeReader &operator()(const std::string &key, T &&arg) {
            process(key, std::forward<T>(arg));
            return *this;
        }

        template<typename T>
        inline YamlRecipeReader &operator()(std::pair<std::string, T> &arg) {
            arg.first = _stack.back()->name();
            if(_ignoreKeyCase) {
                arg.first = util::lower(arg.first);
            }
            inplaceMap(_stack.back()->find(arg.first));
            process(arg.second);
            _stack.pop_back();
            ++(*_stack.back());
            return *this;
        }

        inline YamlRecipeReader &operator()(std::pair<std::string, Object> &arg) {
            arg.first = _stack.back()->name();
            if(_ignoreKeyCase) {
                arg.first = util::lower(arg.first);
            }
            inplaceMap(_stack.back()->find(arg.first));
            auto node = _stack.back()->value();
            if(node.IsScalar()) {
                arg.second = node.as<std::string>();
            } else {
                auto data = std::make_shared<data::SharedStruct>(scope::context());
                auto reader = conv::YamlReader(scope::context(), data);
                reader.begin(node);
                arg.second = data;
            }
            ++(*_stack.back());
            return *this;
        }

        void read(const std::filesystem::path &path) {
            std::ifstream stream{path};
            stream.exceptions(std::ios::failbit | std::ios::badbit);
            if(!stream.is_open()) {
                throw std::runtime_error("Unable to read config file");
            }
            read(stream);
        }

        void read(std::ifstream &stream) {
            // TODO: clear stack?
            _stack.clear();
            YAML::Node node = YAML::Load(stream);
            if(!inplaceMap(node)) {
                throw std::runtime_error("Expecting a map or sequence");
            }
        }

        bool inplaceMap(const YAML::Node &node) {
            if(!(node.IsMap() || node.IsSequence())) {
                return false; // optional map or sequence
            }
            if(node.IsMap()) {
                _stack.emplace_back(std::make_unique<MapIterator>(node.begin(), node.end()));
            } else {
                _stack.emplace_back(std::make_unique<SequenceIterator>(node.begin(), node.end()));
            }
            _stack.back()->setIgnoreKeyCase(_ignoreKeyCase);
            return true;
        }

        template<typename T>
        inline void process(T &head) {
            load(head);
        }

        template<typename T>
        inline void process(const std::string &key, T &head) {

            auto dispatchFn = [this, &key](auto fn, auto &...args) {
                start(key);
                this->*fn(args...);
                end();
            };

            if constexpr(util::is_specialization<T, std::vector>::value) {
                auto exists = start(key);
                if(exists) {
                    load(key, head);
                }
            } else if constexpr(util::is_specialization<T, std::unordered_map>::value) {
                auto exists = start(key);
                if(exists) {
                    load(head);
                    end();
                }
            } else if constexpr(std::is_base_of_v<data::StructModelBase, T>) {
                auto exists = start(key);
                if(exists) {
                    load(head);
                    end();
                }
            } else if constexpr(std::is_base_of_v<conv::Serializable, T>) {
                auto exists = start(key);
                if(exists) {
                    load(head);
                    end();
                }
            } else {
                load(key, head);
            }
        }

        template<typename ArchiveType, typename T>
        void apply(ArchiveType &ar, T &head) {
            head.serialize(ar);
        }

        template<typename T>
        void load(T &head) {
            apply(*this, head);
        }

        template<
            typename T,
            typename = std::enable_if_t<std::is_base_of_v<data::StructModelBase, T>>>
        void load(std::shared_ptr<T> &head) {
            if(!head) {
                head = std::make_shared<T>(scope::context());
            }
            for(auto i = 0; i < _stack.back()->size(); i++) {
                auto node = _stack.back()->value();
                auto reader = conv::YamlReader(scope::context(), head);
                reader.begin(node);
                ++(*_stack.back());
            }
        }

        template<typename T>
        std::enable_if_t<std::is_base_of_v<conv::Serializable, T>> load(
            const std::string &key, std::vector<T> &head) {
            head.resize(_stack.back()->size());
            for(auto &&v : head) {
                inplaceMap(_stack.back()->value());
                (*this)(v);
                _stack.pop_back();
                ++(*_stack.back());
            }
            end();
        }

        template<typename T>
        std::enable_if_t<!std::is_base_of_v<conv::Serializable, T>> load(
            const std::string &key, std::vector<T> &head) {
            head.resize(_stack.back()->size());
            for(auto &v : head) {
                auto node = _stack.back()->value();
                rawValue(node, v);
                ++(*_stack.back());
            }
            _stack.pop_back();
        }

        template<typename T>
        void load(std::unordered_map<std::string, T> &head) {
            head.clear();

            auto hint = head.begin();
            for(auto i = 0; i < _stack.back()->size(); i++) {
                std::string key{};
                T value{};
                auto kv = std::make_pair(key, value);
                (*this)(kv);
                head.emplace_hint(hint, kv);
            }
        }

        template<typename T>
        void load(const std::string &key, T &data) {
            auto node = _stack.back()->find(key);
            // if not scalar then ignore (optional)
            if(node.IsScalar()) {
                rawValue(node, data);
            }
        }

        bool start(const std::string &key) {
            return inplaceMap(_stack.back()->find(key));
        }

        void end() {
            _stack.pop_back();
            ++(*_stack.back());
        }

        // NOLINTNEXTLINE(*-no-recursion)
        template<typename T>
        void rawValue(YAML::Node &node, T &data) {
            if constexpr(std::is_same_v<T, bool>) {
                data = node.as<bool>();
            }
            if constexpr(std::is_integral_v<T>) {
                data = node.as<int>();
            }
            if constexpr(std::is_floating_point_v<T>) {
                data = node.as<double>();
            } else {
                data = node.as<std::string>();
            }
        }
    };
} // namespace config
