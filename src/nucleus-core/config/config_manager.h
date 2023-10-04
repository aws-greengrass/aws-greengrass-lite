#pragma once

#include "data/handle_table.h"
#include "data/safe_handle.h"
#include "data/shared_struct.h"
#include "data/string_table.h"
#include "tasks/expire_time.h"
#include "watcher.h"
#include <filesystem>
#include <optional>
#include <utility>

namespace config {
    class Timestamp;
    class Element;
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
        int64_t _time; // since epoch

    public:
        constexpr Timestamp(const Timestamp &time) = default;
        constexpr Timestamp(Timestamp &&time) = default;

        constexpr Timestamp() : _time{0} {
        }

        explicit constexpr Timestamp(int64_t timeMillis) : _time{timeMillis} {
        }

        template<typename T>
        explicit constexpr Timestamp(const std::chrono::time_point<T> time)
            : _time(static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch())
                    .count()
            )) {
        }

        ~Timestamp() = default;

        static Timestamp now() {
            return Timestamp{std::chrono::system_clock::now()};
        }

        [[nodiscard]] constexpr int64_t asMilliseconds() const noexcept {
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
    };

    constexpr Timestamp Timestamp::never() {
        return Timestamp{0};
    }

    constexpr Timestamp Timestamp::dawn() {
        return Timestamp{1};
    }

    constexpr Timestamp Timestamp::infinite() {
        return Timestamp{-1};
    }

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
    // Extend structure element to include name & time
    // not entirely parallels GG-Java "Topic"
    //
    class Element : public data::StructElement {
    protected:
        data::StringOrd _nameOrd;
        Timestamp _modtime;

    public:
        Element() = default;
        Element(const Element &el) = default;
        Element(Element &&el) = default;
        Element &operator=(const Element &other) = default;
        Element &operator=(Element &&other) = default;
        ~Element() = default;

        explicit Element(const StructElement &se) : data::StructElement(se) {
        }

        explicit Element(data::StringOrd ord, const StructElement &se)
            : data::StructElement(se), _nameOrd{ord} {
        }

        explicit Element(data::StringOrd ord, const Timestamp &timestamp)
            : _nameOrd{ord}, _modtime{timestamp} {
        }

        explicit Element(data::StringOrd ord, const Timestamp &timestamp, data::ValueType newVal)
            : StructElement(std::move(newVal)), _nameOrd{ord}, _modtime{timestamp} {
        }

        explicit Element(
            data::StringOrd ord, const Timestamp &timestamp, const std::shared_ptr<Topics> &topics
        );

        data::StringOrd getNameOrd() {
            return _nameOrd;
        }

        Timestamp getModTime() {
            return _modtime;
        }

        Element &setOrd(data::StringOrd ord) {
            _nameOrd = ord;
            return *this;
        }

        Element &setName(data::Environment &env, const std::string &str);

        Element &setModTime(const Timestamp &modTime) {
            _modtime = modTime;
            return *this;
        }

        [[nodiscard]] data::StringOrd getKey(data::Environment &env) const;
        static data::StringOrd getKey(data::Environment &env, data::StringOrd ord);

        [[nodiscard]] StructElement slice() {
            return StructElement(_value);
        }

        [[nodiscard]] bool isTopics() const {
            return isType<Topics>();
        }

        [[nodiscard]] std::shared_ptr<Topics> getTopicsRef() const {
            return castContainer<Topics>();
        }
    };

    //
    // Set of key/value pairs
    // Somewhat parallels GG-Java "Topics"
    //
    class Topics : public data::StructModelBase {
    protected:
        data::StringOrd _key;
        bool _excludeTlog{false};
        Timestamp _modtime;
        std::weak_ptr<Topics> _parent;
        std::map<data::StringOrd, Element, data::StringOrd::CompLess> _children;
        std::vector<Watching> _watching;
        mutable std::shared_mutex _mutex;

        void rootsCheck(const data::ContainerModelBase *target) const override;

    public:
        explicit Topics(
            data::Environment &environment,
            const std::shared_ptr<Topics> &parent,
            const data::StringOrd &key
        );

        [[nodiscard]] data::StringOrd getKeyOrd() const {
            return _key;
        }

        [[nodiscard]] std::string getKey() const;

        [[nodiscard]] std::vector<std::string> getKeyPath() const;

        [[nodiscard]] bool excludeTlog() {
            return _excludeTlog;
        }

        [[nodiscard]] Timestamp getModTime() const {
            return _modtime;
        }

        void addWatcher(
            data::StringOrd subKey, const std::shared_ptr<Watcher> &watcher, WhatHappened reasons
        );

        void addWatcher(const std::shared_ptr<Watcher> &watcher, WhatHappened reasons);

        bool hasWatchers() const;

        std::optional<std::vector<std::shared_ptr<Watcher>>>
            filterWatchers(data::StringOrd subKey, WhatHappened reasons) const;

        std::optional<std::vector<std::shared_ptr<Watcher>>> filterWatchers(WhatHappened reasons
        ) const;

        void put(data::StringOrd handle, const data::StructElement &element) override;

        void put(std::string_view sv, const data::StructElement &element) override;

        void updateChild(const Element &element);

        bool hasKey(data::StringOrd handle) const override;

        std::vector<data::StringOrd> getKeys() const;

        uint32_t size() const override;

        data::StructElement get(data::StringOrd handle) const override;

        data::StructElement get(std::string_view name) const override;

        std::shared_ptr<data::StructModelBase> copy() const override;

        Element createChild(
            data::StringOrd nameOrd, const std::function<Element(data::StringOrd)> &creator
        );

        Topic createChild(data::StringOrd nameOrd, const Timestamp &timestamp = Timestamp());

        Topic createChild(std::string_view name, const Timestamp &timestamp = Timestamp());

        std::shared_ptr<Topics> createInteriorChild(
            data::StringOrd nameOrd, const Timestamp &timestamp = Timestamp::now()
        );

        std::shared_ptr<Topics> createInteriorChild(
            std::string_view name, const Timestamp &timestamp = Timestamp::now()
        );

        std::vector<std::shared_ptr<Topics>> getInteriors();

        std::vector<Topic> getLeafs();

        Element getChildElement(data::StringOrd handle) const;

        Element getChildElement(std::string_view sv) const;

        Topic getChild(data::StringOrd handle);

        Topic getChild(std::string_view name);

        size_t getSize() const;

        Lookup lookup();

        Lookup lookup(Timestamp timestamp);

        std::optional<data::ValueType> validate(
            data::StringOrd subKey,
            const data::ValueType &proposed,
            const data::ValueType &currentValue
        );
        void notifyChange(data::StringOrd subKey, WhatHappened changeType);
        void notifyChange(WhatHappened changeType);
    };

    inline Element::Element(
        data::StringOrd ord,
        const config::Timestamp &timestamp,
        const std::shared_ptr<Topics> &topics
    )
        : data::StructElement(std::static_pointer_cast<data::ContainerModelBase>(topics)),
          _nameOrd(ord), _modtime(timestamp) {
    }

    class Topic {
        data::Environment *_environment;
        std::shared_ptr<Topics> _parent;
        Element _value;

    public:
        Topic(const Topic &el) = default;
        Topic(Topic &&el) = default;

        Topic &operator=(const Topic &other) = default;

        Topic &operator=(Topic &&other) = default;

        ~Topic() = default;

        explicit Topic(data::Environment &env, const std::shared_ptr<Topics> &parent, Element value)
            : _environment{&env}, _parent{parent}, _value{std::move(value)} {
        }

        [[nodiscard]] data::StringOrd getKeyOrd() const {
            return _value.getKey(*_environment);
        }

        [[nodiscard]] std::shared_ptr<Topics> getTopics() {
            return _parent;
        }

        // signature follows that of GG-Java
        Topic &withNewerValue(
            const Timestamp &proposedModTime,
            data::ValueType proposed,
            bool allowTimestampToDecrease = false,
            bool allowTimestampToIncreaseWhenValueHasntChanged = false
        );

        // signature follows that of GG-Java
        Topic &withValue(data::ValueType nv) {
            return withNewerValue(Timestamp::now(), std::move(nv));
        }

        // signature follows that of GG-Java
        Topic &overrideValue(data::ValueType nv) {
            return withNewerValue(_value.getModTime(), std::move(nv));
        }

        Topic &addWatcher(const std::shared_ptr<Watcher> &watcher, WhatHappened reasons);
        Topic &dflt(data::ValueType defVal);

        Element get() {
            return _value;
        }

        uint64_t getInt() {
            return get().getInt();
        }

        std::string getString() {
            return get().getString();
        }
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

        [[nodiscard]] Topic get(data::StringOrd ord) {
            return _root->getChild(ord);
        }

        [[nodiscard]] Topic get(std::string_view sv) {
            return _root->getChild(sv);
        }

        [[nodiscard]] Element element(data::StringOrd ord) const {
            return _root->getChildElement(ord);
        }

        [[nodiscard]] Element element(std::string_view sv) const {
            return _root->getChildElement(sv);
        }
    };

    class Manager {
    private:
        data::Environment &_environment;
        std::shared_ptr<Topics> _root;

    public:
        explicit Manager(data::Environment &environment)
            : _environment{environment},
              _root{std::make_shared<Topics>(environment, nullptr, data::StringOrd::nullHandle())} {
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
