#pragma once

#include "config/config_manager.hpp"
#include "data/symbol_value_map.hpp"

namespace scope {
    class Context;
}

namespace config {
    class Timestamp;
    class TopicElement;
    class Topic;
    class Topics;

    //
    // Container class for watches on a given topic
    //
    class Watching {
        data::Symbol _subKey{}; // if specified, indicates value that is being
                                // watched
        WhatHappened _reasons{WhatHappened::never}; // bitmask of reasons to fire
                                                    // on
        std::weak_ptr<Watcher> _watcher{}; // handler (base class) - may go away

    public:
        Watching() = default;
        Watching(const Watching &) = default;
        Watching(Watching &&) = default;
        Watching &operator=(const Watching &) = default;
        Watching &operator=(Watching &&) = default;
        ~Watching() = default;

        Watching(data::Symbol subKey, const std::shared_ptr<Watcher> &watcher, WhatHappened reasons)
            : _subKey{subKey}, _watcher{watcher}, _reasons{reasons} {
        }

        Watching(const std::shared_ptr<Watcher> &watcher, WhatHappened reasons)
            : Watching({}, watcher, reasons) {
        }

        [[nodiscard]] bool shouldFire(data::Symbol subKey, WhatHappened whatHappened) const {
            return (_reasons & whatHappened) != WhatHappened::never && _subKey == subKey;
        }

        [[nodiscard]] bool expired() const {
            return _watcher.expired();
        }

        std::shared_ptr<Watcher> watcher() {
            return _watcher.lock();
        }
    };

    //
    // GG-Interop: Subset of functionality of Node, provided as a mixin interface
    //
    class ConfigNode {
    public:
        ConfigNode() noexcept = default;
        ConfigNode(const ConfigNode &) noexcept = default;
        ConfigNode(ConfigNode &&) noexcept = default;
        virtual ~ConfigNode() noexcept = default;
        ConfigNode &operator=(const ConfigNode &) noexcept = default;
        ConfigNode &operator=(ConfigNode &&) noexcept = default;
        [[nodiscard]] virtual data::Symbol getNameOrd() const = 0;
        [[nodiscard]] virtual std::string getName() const = 0;
        [[nodiscard]] virtual Timestamp getModTime() const = 0;
        [[nodiscard]] virtual std::shared_ptr<Topics> getParent() = 0;
        virtual void remove() = 0;
        virtual void remove(const Timestamp &timestamp) = 0;
        [[nodiscard]] virtual bool excludeTlog() const = 0;
        [[nodiscard]] virtual std::vector<std::string> getKeyPath() const = 0;
    };

    //
    // Element is typically used to store leaf nodes (see Topic as the main extension of this)
    //
    class TopicElement : public data::StructElement {
    protected:
        data::Symbol _name;
        Timestamp _modtime;

    public:
        TopicElement() = default;
        TopicElement(const TopicElement &el) = default;
        TopicElement(TopicElement &&el) = default;
        explicit TopicElement(const Topic &topic);
        TopicElement &operator=(const TopicElement &other) = default;
        TopicElement &operator=(TopicElement &&other) = default;
        ~TopicElement() override = default;

        explicit TopicElement(
            data::Symbol ord, const Timestamp &timestamp, const data::StructElement &newVal)
            : StructElement(newVal), _name{ord}, _modtime{timestamp} {
        }

        TopicElement(data::Symbol ord, const Timestamp &timestamp, const data::ValueType &newVal)
            : StructElement(newVal), _name{ord}, _modtime{timestamp} {
        }

        [[nodiscard]] data::Symbol getKey() const;
        static data::Symbol getKey(data::Symbol key);

        [[nodiscard]] StructElement slice() const {
            return {_value};
        }
    };

    //
    // Set of key/value pairs
    // GG-Interop: Compare with Java Topics
    //
    class Topics : public data::StructModelBase, public ConfigNode {
    private:
        scope::SharedContextMapper _symbolMapper;
        data::Symbol _nameOrd;
        Timestamp _modtime;
        std::atomic_bool _excludeTlog{false};
        bool _notifyParent{true};
        std::weak_ptr<Topics> _parent;
        data::SymbolValueMap<TopicElement> _children{_symbolMapper};
        std::vector<Watching> _watching;
        mutable std::shared_mutex _mutex;

        void rootsCheck(const data::ContainerModelBase *target) const override;
        void updateChild(const TopicElement &element);
        TopicElement createChild(
            data::Symbol nameOrd, const std::function<TopicElement(data::Symbol)> &creator);
        void publish(const PublishAction &action);
        [[nodiscard]] std::string getNameUnsafe() const;

    public:
        explicit Topics(
            const std::shared_ptr<scope::Context> &context,
            const std::shared_ptr<Topics> &parent,
            const data::Symbol &key,
            const Timestamp &modtime);

        // Overrides for ConfigNode

        [[nodiscard]] data::Symbol getNameOrd() const override;
        [[nodiscard]] Timestamp getModTime() const override;
        [[nodiscard]] std::shared_ptr<Topics> getParent() override;
        [[nodiscard]] std::string getName() const override;
        [[nodiscard]] std::vector<std::string> getKeyPath() const override;
        void remove() override;
        void remove(const Timestamp &timestamp) override;
        [[nodiscard]] bool excludeTlog() const override;

        // Overrides for StructModelBase
        // Don't use directly, but behave correctly when used via API

        void putImpl(data::Symbol handle, const data::StructElement &element) override;
        data::StructElement getImpl(data::Symbol handle) const override;
        bool hasKeyImpl(data::Symbol handle) const override;
        [[nodiscard]] std::vector<data::Symbol> getKeys() const override;
        uint32_t size() const override;
        std::shared_ptr<data::StructModelBase> copy() const override;

        // Watchers/Publishing

        void addWatcher(
            data::Symbol subKey, const std::shared_ptr<Watcher> &watcher, WhatHappened reasons);
        void addWatcher(const std::shared_ptr<Watcher> &watcher, WhatHappened reasons);
        bool hasWatchers() const;
        bool parentNeedsToKnow() const;
        void setParentNeedsToKnow(bool f);

        std::optional<std::vector<std::shared_ptr<Watcher>>> filterWatchers(
            data::Symbol subKey, WhatHappened reasons) const;

        std::optional<std::vector<std::shared_ptr<Watcher>>> filterWatchers(
            WhatHappened reasons) const;
        void notifyChange(data::Symbol subKey, WhatHappened changeType);
        void notifyChange(WhatHappened changeType);
        std::optional<data::ValueType> validate(
            data::Symbol subKey,
            const data::ValueType &proposed,
            const data::ValueType &currentValue);

        // Child manipulation used in context of configuration

        void updateChild(const Topic &element);
        std::shared_ptr<ConfigNode> getNode(data::Symbol handle);
        std::shared_ptr<ConfigNode> getNode(std::string_view name);
        std::shared_ptr<ConfigNode> getNode(const std::vector<std::string> &path);
        Topic createTopic(data::Symbol nameOrd, const Timestamp &timestamp = Timestamp());
        Topic createTopic(std::string_view name, const Timestamp &timestamp = Timestamp());
        std::shared_ptr<Topics> createInteriorChild(
            data::Symbol nameOrd, const Timestamp &timestamp = Timestamp::now());
        std::shared_ptr<Topics> createInteriorChild(
            std::string_view name, const Timestamp &timestamp = Timestamp::now());
        std::shared_ptr<Topics> findInteriorChild(std::string_view name);
        std::shared_ptr<Topics> findInteriorChild(data::Symbol handle);
        std::vector<std::shared_ptr<Topics>> getInteriors();
        std::vector<Topic> getLeafs();
        Topic getTopic(data::Symbol handle);
        Topic getTopic(std::string_view name);
        Topic lookup(const std::vector<std::string> &path);
        Topic lookup(Timestamp timestamp, const std::vector<std::string> &path);
        std::shared_ptr<Topics> lookupTopics(const std::vector<std::string> &path);
        std::shared_ptr<Topics> lookupTopics(
            Timestamp timestamp, const std::vector<std::string> &path);
        std::optional<Topic> find(std::initializer_list<std::string> path);
        data::ValueType findOrDefault(
            const data::ValueType &, std::initializer_list<std::string> path);
        std::shared_ptr<Topics> findTopics(std::initializer_list<std::string> path);
        void removeChild(ConfigNode &node);
    };

    //
    // Topic essentially is the leaf equivalent of Topics, decorated with additional
    // information needed for behavior as a ConfigNode
    //
    class Topic : public TopicElement, public ConfigNode {
    protected:
        std::weak_ptr<scope::Context> _context;
        std::shared_ptr<Topics> _parent;

        scope::Context &context() {
            return *_context.lock();
        }

    public:
        Topic(const Topic &el) = default;
        Topic(Topic &&el) = default;
        Topic &operator=(const Topic &other) = default;
        Topic &operator=(Topic &&other) = default;
        ~Topic() override = default;

        explicit operator bool() const {
            return static_cast<bool>(_name);
        }

        bool operator!() const {
            return !_name;
        }

        explicit Topic(
            const std::shared_ptr<scope::Context> &context,
            const std::shared_ptr<Topics> &parent,
            const TopicElement &value)
            : _context{context}, _parent{parent}, TopicElement{value} {
        }

        [[nodiscard]] data::Symbol getNameOrd() const override {
            return _name;
        }

        [[nodiscard]] std::string getName() const override;
        [[nodiscard]] std::vector<std::string> getKeyPath() const override;

        [[nodiscard]] Timestamp getModTime() const override {
            return _modtime;
        }

        [[nodiscard]] std::shared_ptr<Topics> getParent() override {
            return _parent;
        }

        void remove() override;
        void remove(const Timestamp &timestamp) override;
        [[nodiscard]] bool excludeTlog() const override;

        // GG-Interop: Signature follows that of GG-Java
        Topic &withNewerValue(
            const Timestamp &proposedModTime,
            data::ValueType proposed,
            bool allowTimestampToDecrease = false,
            bool allowTimestampToIncreaseWhenValueHasntChanged = false);

        Topic &withNewerModTime(const Timestamp &newModTime);

        // GG-Interop: Signature follows that of GG-Java
        Topic &withValue(data::ValueType nv) {
            return withNewerValue(Timestamp::now(), std::move(nv));
        }

        // GG-Interop: Signature follows that of GG-Java
        Topic &overrideValue(data::ValueType nv) {
            return withNewerValue(_modtime, std::move(nv));
        }

        Topic &addWatcher(const std::shared_ptr<Watcher> &watcher, WhatHappened reasons);
        Topic &dflt(data::ValueType defVal);
    };
} // namespace config
