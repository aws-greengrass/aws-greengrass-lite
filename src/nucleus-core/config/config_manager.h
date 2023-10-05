#pragma once

#include "data/handle_table.h"
#include "data/safe_handle.h"
#include "data/shared_struct.h"
#include "data/string_table.h"
#include "tasks/expire_time.h"
#include "watcher.h"
#include <atomic>
#include <filesystem>
#include <optional>
#include <utility>

namespace config {
    class Timestamp;
    class TopicElement;
    class Topic;
    class Topics;
    class Lookup;

    //
    // GG-Java use of config timestamp can be considered to be a signed long
    // representing milliseconds since epoch. Given the special constants, it's
    // better to handle as 64-bit signed integer rather than handle all the weird
    // edge conditions.
    //
    class Timestamp {
    private:
        uint64_t _time; // since epoch

    public:
        constexpr Timestamp(const Timestamp &time) = default;
        constexpr Timestamp(Timestamp &&time) = default;

        constexpr Timestamp() : _time{0} {
        }

        explicit constexpr Timestamp(uint64_t timeMillis) : _time{timeMillis} {
        }

        template<typename T>
        explicit constexpr Timestamp(const std::chrono::time_point<T> time)
            : _time(static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch())
                    .count()
            )) {
        }

        ~Timestamp() = default;

        static Timestamp now() {
            return Timestamp{std::chrono::system_clock::now()};
        }

        [[nodiscard]] constexpr uint64_t asMilliseconds() const noexcept {
            return _time;
        };

        constexpr bool operator==(const Timestamp &other) const noexcept {
            return _time == other._time;
        }

        constexpr bool operator!=(const Timestamp &other) const noexcept {
            return _time != other._time;
        }

        constexpr bool operator<(const Timestamp &other) const noexcept {
            return _time < other._time;
        }

        constexpr bool operator<=(const Timestamp &other) const noexcept {
            return _time <= other._time;
        }

        constexpr bool operator>(const Timestamp &other) const noexcept {
            return _time > other._time;
        }

        constexpr bool operator>=(const Timestamp &other) const noexcept {
            return _time >= other._time;
        }

        constexpr Timestamp &operator=(const Timestamp &time) = default;
        constexpr Timestamp &operator=(Timestamp &&time) = default;

        static constexpr Timestamp never();
        static constexpr Timestamp dawn();
        static constexpr Timestamp infinite();
        static Timestamp ofFile(std::filesystem::file_time_type fileTime);
    };

    inline constexpr Timestamp Timestamp::never() {
        return Timestamp{0};
    }

    inline constexpr Timestamp Timestamp::dawn() {
        return Timestamp{1};
    }

    inline constexpr Timestamp Timestamp::infinite() {
        return Timestamp{std::numeric_limits<uint64_t>::max()};
    }

    inline Timestamp Timestamp::ofFile(std::filesystem::file_time_type fileTime) {
        // C++17 hack, there is no universal way to convert from file-time to sys-time
        // Because 'now' is obtained twice, time is subject to slight error
        // C++20 fixes this with file_clock::to_sys
        auto sysTimeNow = std::chrono::system_clock::now();
        auto fileTimeNow = std::filesystem::file_time_type::clock::now();
        return Timestamp(
            sysTimeNow
            + std::chrono::duration_cast<std::chrono::milliseconds>(fileTime - fileTimeNow)
        );
    }

    //
    // Container class for watches on a given topic
    //
    class Watching {
        data::StringOrd _subKey{}; // if specified, indicates value that is being
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

        Watching(
            data::StringOrd subKey, const std::shared_ptr<Watcher> &watcher, WhatHappened reasons
        )
            : _subKey{subKey}, _watcher{watcher}, _reasons{reasons} {
        }

        Watching(const std::shared_ptr<Watcher> &watcher, WhatHappened reasons)
            : Watching({}, watcher, reasons) {
        }

        [[nodiscard]] bool shouldFire(data::StringOrd subKey, WhatHappened whatHappened) const {
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
        [[nodiscard]] virtual data::StringOrd getNameOrd() const = 0;
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
        data::StringOrd _nameOrd;
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
            data::StringOrd ord, const Timestamp &timestamp, const data::StructElement &newVal
        )
            : StructElement(newVal), _nameOrd{ord}, _modtime{timestamp} {
        }

        explicit TopicElement(
            data::StringOrd ord, const Timestamp &timestamp, const data::ValueType &newVal
        )
            : StructElement(newVal), _nameOrd{ord}, _modtime{timestamp} {
        }

        [[nodiscard]] data::StringOrd getKey(data::Environment &env) const;
        static data::StringOrd getKey(data::Environment &env, data::StringOrd ord);

        [[nodiscard]] StructElement slice() const {
            return StructElement(_value);
        }
    };

    //
    // Set of key/value pairs
    // GG-Interop: Compare with Java Topics
    //
    class Topics : public data::StructModelBase, public ConfigNode {
    private:
        data::StringOrd _nameOrd;
        Timestamp _modtime;
        std::atomic_bool _excludeTlog{false};
        std::weak_ptr<Topics> _parent;
        std::map<data::StringOrd, TopicElement, data::StringOrd::CompLess> _children;
        std::vector<Watching> _watching;
        mutable std::shared_mutex _mutex;

        void rootsCheck(const data::ContainerModelBase *target) const override;
        void updateChild(const TopicElement &element);
        TopicElement createChild(
            data::StringOrd nameOrd, const std::function<TopicElement(data::StringOrd)> &creator
        );

    public:
        explicit Topics(
            data::Environment &environment,
            const std::shared_ptr<Topics> &parent,
            const data::StringOrd &key,
            const Timestamp &modtime
        );

        // Overrides for ConfigNode

        [[nodiscard]] data::StringOrd getNameOrd() const override;
        [[nodiscard]] Timestamp getModTime() const override;
        [[nodiscard]] std::shared_ptr<Topics> getParent() override;
        [[nodiscard]] std::string getName() const override;
        [[nodiscard]] std::vector<std::string> getKeyPath() const override;
        void remove() override;
        void remove(const Timestamp &timestamp) override;
        [[nodiscard]] bool excludeTlog() const override;

        // Overrides for StructModelBase
        // Don't use directly, but behave correctly when used via API

        void put(data::StringOrd handle, const data::StructElement &element) override;
        void put(std::string_view sv, const data::StructElement &element) override;
        data::StructElement get(data::StringOrd handle) const override;
        data::StructElement get(std::string_view name) const override;
        bool hasKey(data::StringOrd handle) const override;
        [[nodiscard]] std::vector<data::StringOrd> getKeys() const override;
        uint32_t size() const override;
        std::shared_ptr<data::StructModelBase> copy() const override;

        // Watchers/Publishing

        void addWatcher(
            data::StringOrd subKey, const std::shared_ptr<Watcher> &watcher, WhatHappened reasons
        );
        void addWatcher(const std::shared_ptr<Watcher> &watcher, WhatHappened reasons);
        bool hasWatchers() const;

        std::optional<std::vector<std::shared_ptr<Watcher>>> filterWatchers(
            data::StringOrd subKey, WhatHappened reasons
        ) const;

        std::optional<std::vector<std::shared_ptr<Watcher>>> filterWatchers(WhatHappened reasons
        ) const;
        void notifyChange(data::StringOrd subKey, WhatHappened changeType);
        void notifyChange(WhatHappened changeType);
        std::optional<data::ValueType> validate(
            data::StringOrd subKey,
            const data::ValueType &proposed,
            const data::ValueType &currentValue
        );

        // Child manipulation used in context of configuration

        void updateChild(const Topic &element);
        std::shared_ptr<ConfigNode> getNode(data::StringOrd handle);
        std::shared_ptr<ConfigNode> getNode(std::string_view name);
        Topic createTopic(data::StringOrd nameOrd, const Timestamp &timestamp = Timestamp());
        Topic createTopic(std::string_view name, const Timestamp &timestamp = Timestamp());
        std::shared_ptr<Topics> createInteriorChild(
            data::StringOrd nameOrd, const Timestamp &timestamp = Timestamp::now()
        );
        std::shared_ptr<Topics> createInteriorChild(
            std::string_view name, const Timestamp &timestamp = Timestamp::now()
        );
        std::vector<std::shared_ptr<Topics>> getInteriors();
        std::vector<Topic> getLeafs();
        Topic getTopic(data::StringOrd handle);
        Topic getTopic(std::string_view name);
        Lookup lookup();
        Lookup lookup(Timestamp timestamp);
        void removeChild(ConfigNode &node);
    };

    //
    // Topic essentially is the leaf equivalent of Topics, decorated with additional
    // information needed for behavior as a ConfigNode
    //
    class Topic : public TopicElement, public ConfigNode {
    protected:
        data::Environment *_environment; // By-ref to allow copying
        std::shared_ptr<Topics> _parent;

    public:
        Topic(const Topic &el) = default;
        Topic(Topic &&el) = default;
        Topic &operator=(const Topic &other) = default;
        Topic &operator=(Topic &&other) = default;
        ~Topic() override = default;

        explicit operator bool() const override {
            return static_cast<bool>(_nameOrd);
        }

        bool operator!() const override {
            return !_nameOrd;
        }

        explicit Topic(
            data::Environment &env, const std::shared_ptr<Topics> &parent, const TopicElement &value
        )
            : _environment{&env}, _parent{parent}, TopicElement{value} {
        }

        [[nodiscard]] data::StringOrd getNameOrd() const override {
            return _nameOrd;
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
            bool allowTimestampToIncreaseWhenValueHasntChanged = false
        );

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

    class Lookup {
        data::Environment *_environment;
        std::shared_ptr<Topics> _root;
        Timestamp _interiorTimestamp{};
        Timestamp _leafTimestamp{};

    public:
        Lookup(const Lookup &el) = default;
        Lookup(Lookup &&el) = default;
        Lookup &operator=(const Lookup &other) = default;
        Lookup &operator=(Lookup &&other) = default;
        ~Lookup() = default;

        explicit Lookup(
            data::Environment &env,
            const std::shared_ptr<Topics> &root,
            const Timestamp &interiorTimestamp,
            const Timestamp &leafTimestamp
        )
            : _environment{&env}, _root{root}, _interiorTimestamp{interiorTimestamp},
              _leafTimestamp{leafTimestamp} {
        }

        [[nodiscard]] Lookup &operator[](data::StringOrd ord) {
            _root = _root->createInteriorChild(ord, _interiorTimestamp);
            return *this;
        }

        [[nodiscard]] Lookup &operator[](std::string_view sv) {
            _root = _root->createInteriorChild(sv, _interiorTimestamp);
            return *this;
        }

        [[nodiscard]] Lookup &operator[](const std::vector<std::string> &path) {
            for(const auto &it : path) {
                _root = _root->createInteriorChild(it, _interiorTimestamp);
            }
            return *this;
        }

        [[nodiscard]] Topic operator()(data::StringOrd ord) {
            return _root->createTopic(ord, _leafTimestamp);
        }

        [[nodiscard]] Topic operator()(std::string_view sv) {
            return _root->createTopic(sv, _leafTimestamp);
        }

        [[nodiscard]] Topic operator()(const std::vector<std::string> &path) {
            if(path.empty()) {
                throw std::runtime_error("Empty path provided");
            }
            auto steps = path.size();
            auto it = path.begin();
            while(--steps > 0) {
                _root = _root->createInteriorChild(*it, _interiorTimestamp);
                ++it;
            }
            return _root->createTopic(*it, _leafTimestamp);
        }

        [[nodiscard]] Topic getTopic(data::StringOrd ord) {
            return _root->getTopic(ord);
        }

        [[nodiscard]] Topic getTopic(std::string_view sv) {
            return _root->getTopic(sv);
        }

        [[nodiscard]] std::shared_ptr<ConfigNode> getNode(const std::vector<std::string> &path) {
            if(path.empty()) {
                throw std::runtime_error("Empty path provided");
            }
            std::shared_ptr<ConfigNode> node{_root};
            auto it = path.begin();
            for(; it != path.end(); ++it) {
                std::shared_ptr<Topics> t{std::dynamic_pointer_cast<Topics>(node)};
                if(!t) {
                    return {};
                }
                node = t->getNode(*it);
            }
            return node;
        }
    };

    class Manager {
    private:
        data::Environment &_environment;
        std::shared_ptr<Topics> _root;

    public:
        explicit Manager(data::Environment &environment)
            : _environment{environment},
              _root{std::make_shared<Topics>(
                  environment, nullptr, data::StringOrd::nullHandle(), Timestamp::never()
              )} {
        }

        std::shared_ptr<Topics> root() {
            return _root;
        }

        Manager &read(const std::filesystem::path &path);

        Lookup lookup() {
            return _root->lookup();
        }

        Lookup lookup(Timestamp timestamp) {
            return _root->lookup(timestamp);
        }
    };
} // namespace config
