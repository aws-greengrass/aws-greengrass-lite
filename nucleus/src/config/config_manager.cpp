#include "config_manager.hpp"
#include "data/environment.hpp"
#include "transaction_log.hpp"
#include "yaml_helper.hpp"
#include <util.hpp>
#include <utility>

//
// Note that config intake is case insensitive - config comes from
// a settings file (YAML), transaction log (YAML), or cloud (JSON or YAML)
// For optimization, this implementation assumes all config keys are stored
// lower-case which means translation on intake is important
//
namespace config {

    data::StringOrd TopicElement::getKey(data::Environment &env) const {
        return getKey(env, _nameOrd);
    }

    data::StringOrd TopicElement::getKey(data::Environment &env, data::StringOrd nameOrd) {
        if(!nameOrd) {
            return nameOrd;
        }
        std::string str = env.stringTable.getString(nameOrd);
        // a folded string strictly acts on the ascii range and not on international
        // characters this keeps it predictable and handles the problems with GG
        // configs
        std::string lowered = util::lower(str);
        if(str == lowered) {
            return nameOrd;
        } else {
            return env.stringTable.getOrCreateOrd(lowered);
        }
    }

    Topics::Topics(
        data::Environment &environment,
        const std::shared_ptr<Topics> &parent,
        const data::StringOrd &key,
        const Timestamp &modtime
    )
        : data::StructModelBase{environment}, _parent{parent}, _nameOrd{key}, _modtime(modtime) {
        // Note: don't lock parent, it's most likely already locked - atomic used instead
        if((parent && parent->_excludeTlog)
           || (_nameOrd && util::startsWith(getNameUnsafe(), "_"))) {
            _excludeTlog = true;
        }
    }

    std::string Topics::getNameUnsafe() const {
        if(!_nameOrd) {
            return {}; // root
        }
        return _environment.stringTable.getString(_nameOrd);
    }

    std::string Topics::getName() const {
        std::shared_lock guard{_mutex};
        return getNameUnsafe();
    }

    void Topics::updateChild(const Topic &element) {
        updateChild(TopicElement(element));
    }

    void Topics::updateChild(const TopicElement &element) {
        data::StringOrd key = element.getKey(_environment);
        if(element.isType<data::StructModelBase>()) {
            throw std::runtime_error("Not permitted to insert structures/maps");
        }
        checkedPut(element, [this, key, &element](auto &el) {
            std::unique_lock guard{_mutex};
            _children[key] = element;
        });
    }

    void Topics::rootsCheck(const data::ContainerModelBase *target
    ) const { // NOLINT(*-no-recursion)
        if(this == target) {
            throw std::runtime_error("Recursive reference of structure");
        }
        // we don't want to keep nesting locks else we will deadlock
        std::shared_lock guard{_mutex};
        std::vector<std::shared_ptr<data::ContainerModelBase>> structs;
        for(const auto &i : _children) {
            if(i.second.isContainer()) {
                std::shared_ptr<data::ContainerModelBase> otherContainer = i.second.getContainer();
                if(otherContainer) {
                    structs.emplace_back(otherContainer);
                }
            }
        }
        guard.unlock();
        for(const auto &i : structs) {
            i->rootsCheck(target);
        }
    }

    void Topics::addWatcher(const std::shared_ptr<Watcher> &watcher, WhatHappened reasons) {
        addWatcher({}, watcher, reasons);
    }

    void Topics::addWatcher(
        data::StringOrd subKey, const std::shared_ptr<Watcher> &watcher, WhatHappened reasons
    ) {
        if(!watcher) {
            return; // null watcher is a no-op
        }
        data::StringOrd normKey = TopicElement::getKey(_environment, subKey);
        std::unique_lock guard{_mutex};
        // opportunistic check if any watches need deleting - number of watches
        // expected to be small, number of expired watches rare, algorithm for
        // simplicity
        for(auto i = _watching.begin(); i != _watching.end();) {
            if(i->expired()) {
                i = _watching.erase(i);
            } else {
                ++i;
            }
        }
        // add new watcher
        _watching.emplace_back(normKey, watcher, reasons);
        // first call
        guard.unlock();
        watcher->initialized(ref<Topics>(), subKey, reasons);
    }

    Topic &Topic::addWatcher(
        const std::shared_ptr<Watcher> &watcher, config::WhatHappened reasons
    ) {
        _parent->addWatcher(_nameOrd, watcher, reasons);
        return *this;
    }

    bool Topics::hasWatchers() const {
        std::shared_lock guard{_mutex};
        return !_watching.empty();
    }

    bool Topics::parentNeedsToKnow() const {
        std::shared_lock guard{_mutex};
        return _notifyParent && !_excludeTlog && !_parent.expired();
    }

    void Topics::setParentNeedsToKnow(bool f) {
        std::unique_lock guard{_mutex};
        _notifyParent = f;
    }

    std::optional<std::vector<std::shared_ptr<Watcher>>> Topics::filterWatchers(
        config::WhatHappened reasons
    ) const {
        return filterWatchers({}, reasons);
    }

    std::optional<std::vector<std::shared_ptr<Watcher>>> Topics::filterWatchers(
        data::StringOrd key, config::WhatHappened reasons
    ) const {
        if(!hasWatchers()) {
            return {};
        }
        data::StringOrd normKey = TopicElement::getKey(_environment, key);
        std::shared_lock guard{_mutex};
        std::vector<std::shared_ptr<Watcher>> filtered;
        for(auto i : _watching) {
            if(i.shouldFire(normKey, reasons)) {
                std::shared_ptr<Watcher> w = i.watcher();
                if(w) {
                    filtered.push_back(w);
                }
            }
        }
        if(filtered.empty()) {
            return {};
        } else {
            return filtered;
        }
    }

    std::shared_ptr<data::StructModelBase> Topics::copy() const {
        const std::shared_ptr<Topics> parent{_parent};
        std::shared_lock guard{_mutex}; // for source
        std::shared_ptr<Topics> newCopy{
            std::make_shared<Topics>(_environment, parent, _nameOrd, _modtime)};
        for(const auto &i : _children) {
            newCopy->put(i.first, i.second);
        }
        return newCopy;
    }

    void Topics::putImpl(const data::StringOrd handle, const data::StructElement &element) {
        updateChild(TopicElement{handle, Timestamp::never(), element});
    }

    bool Topics::hasKeyImpl(const data::StringOrd handle) const {
        //_environment.stringTable.assertStringHandle(handle);
        data::StringOrd key = TopicElement::getKey(_environment, handle);
        std::shared_lock guard{_mutex};
        auto i = _children.find(key);
        return i != _children.end();
    }

    std::vector<std::string> Topics::getKeyPath() const { // NOLINT(*-no-recursion)
        std::shared_lock guard{_mutex};
        std::shared_ptr<Topics> parent{_parent.lock()};
        std::vector<std::string> path;
        if(parent) {
            path = parent->getKeyPath();
        }
        if(_nameOrd) {
            path.push_back(getName());
        }
        return path;
    }

    std::vector<data::StringOrd> Topics::getKeys() const {
        std::vector<data::StringOrd> keys;
        std::shared_lock guard{_mutex};
        keys.reserve(_children.size());
        for(const auto &_element : _children) {
            keys.emplace_back(_element.first);
        }
        return keys;
    }

    uint32_t Topics::size() const {
        //_environment.stringTable.assertStringHandle(handle);
        std::shared_lock guard{_mutex};
        return _children.size();
    }

    TopicElement Topics::createChild(
        data::StringOrd nameOrd, const std::function<TopicElement(data::StringOrd)> &creator
    ) {
        data::StringOrd key = TopicElement::getKey(_environment, nameOrd);
        std::unique_lock guard{_mutex};
        auto i = _children.find(key);
        if(i != _children.end()) {
            return i->second;
        } else {
            return _children[key] = creator(nameOrd);
        }
    }

    std::shared_ptr<Topics> Topics::createInteriorChild(
        data::StringOrd nameOrd, const Timestamp &timestamp
    ) {
        TopicElement leaf = createChild(nameOrd, [this, &timestamp, nameOrd](auto ord) {
            std::shared_ptr<Topics> parent{ref<Topics>()};
            std::shared_ptr<Topics> nested{
                std::make_shared<Topics>(_environment, parent, nameOrd, timestamp)};
            // Note: Time on TopicElement is ignored for interior children - this is intentional
            return TopicElement(ord, Timestamp::never(), nested);
        });
        return leaf.castObject<Topics>();
    }

    std::shared_ptr<Topics> Topics::createInteriorChild(
        std::string_view sv, const Timestamp &timestamp
    ) {
        data::StringOrd handle = _environment.stringTable.getOrCreateOrd(std::string(sv));
        return createInteriorChild(handle, timestamp);
    }

    std::vector<std::shared_ptr<Topics>> Topics::getInteriors() {
        std::vector<std::shared_ptr<Topics>> interiors;
        std::shared_lock guard{_mutex};
        for(const auto &i : _children) {
            if(i.second.isType<Topics>()) {
                interiors.push_back(i.second.castObject<Topics>());
            }
        }
        return interiors;
    }

    std::vector<Topic> Topics::getLeafs() {
        std::shared_ptr<Topics> self = ref<Topics>();
        std::vector<Topic> leafs;
        std::shared_lock guard{_mutex};
        for(const auto &i : _children) {
            if(!i.second.isType<Topics>()) {
                leafs.emplace_back(_environment, self, i.second);
            }
        }
        return leafs;
    }

    Topic Topics::createTopic(data::StringOrd nameOrd, const Timestamp &timestamp) {
        TopicElement el = createChild(nameOrd, [&](auto ord) {
            return TopicElement(ord, timestamp, data::ValueType{});
        });
        return Topic(_environment, ref<Topics>(), el);
    }

    Topic Topics::createTopic(std::string_view name, const Timestamp &timestamp) {
        data::StringOrd handle = _environment.stringTable.getOrCreateOrd(std::string(name));
        return createTopic(handle, timestamp);
    }

    Topic Topics::getTopic(data::StringOrd handle) {
        data::StringOrd key = TopicElement::getKey(_environment, handle);
        std::shared_lock guard{_mutex};
        auto i = _children.find(key);
        if(i != _children.end()) {
            return Topic(_environment, ref<Topics>(), i->second);
        } else {
            return Topic(_environment, nullptr, {});
        }
    }

    Topic Topics::getTopic(std::string_view name) {
        data::StringOrd handle = _environment.stringTable.getOrCreateOrd(std::string(name));
        return getTopic(handle);
    }

    data::StructElement Topics::getImpl(data::StringOrd handle) const {
        // needed for base class
        data::StringOrd key = TopicElement::getKey(_environment, handle);
        std::shared_lock guard{_mutex};
        auto i = _children.find(key);
        if(i != _children.end()) {
            return i->second.slice();
        } else {
            return {};
        }
    }

    std::shared_ptr<ConfigNode> Topics::getNode(data::StringOrd handle) {
        data::StringOrd key = TopicElement::getKey(_environment, handle);
        std::shared_lock guard{_mutex};
        auto i = _children.find(key);
        if(i != _children.end()) {
            if(i->second.isType<Topics>()) {
                return i->second.castObject<Topics>();
            } else {
                return std::make_shared<Topic>(_environment, ref<Topics>(), i->second);
            }
        } else {
            return {};
        }
    }

    std::shared_ptr<ConfigNode> Topics::getNode(std::string_view sv) {
        data::StringOrd handle = _environment.stringTable.getOrCreateOrd(std::string(sv));
        return getNode(handle);
    }

    Lookup Topics::lookup() {
        return Lookup(_environment, ref<Topics>(), Timestamp::now(), Timestamp::never());
    }

    Lookup Topics::lookup(Timestamp timestamp) {
        return Lookup(_environment, ref<Topics>(), timestamp, timestamp);
    }

    std::optional<data::ValueType> Topics::validate(
        data::StringOrd subKey, const data::ValueType &proposed, const data::ValueType &currentValue
    ) {
        auto watchers = filterWatchers(subKey, WhatHappened::validation);
        if(!watchers.has_value()) {
            return {};
        }
        // Logic follows GG-Java
        bool rewrite = true;
        data::ValueType newValue = proposed;
        // Try to make all the validators happy, but not infinitely
        for(int laps = 3; laps > 0 && rewrite; --laps) {
            rewrite = false;
            for(const auto &i : watchers.value()) {
                std::optional<data::ValueType> nv =
                    i->validate(ref<Topics>(), subKey, newValue, currentValue);
                if(nv.has_value() && nv.value() != newValue) {
                    rewrite = true;
                    newValue = nv.value();
                }
            }
        }
        return newValue;
    }

    void Topics::notifyChange(data::StringOrd subKey, WhatHappened changeType) {
        auto watchers = filterWatchers(subKey, changeType);
        auto self{ref<Topics>()};
        if(watchers.has_value()) {
            for(const auto &i : watchers.value()) {
                publish([i, self, subKey, changeType]() { i->changed(self, subKey, changeType); });
            }
        }

        if(subKey) {
            watchers = filterWatchers(WhatHappened::childChanged);
            if(watchers.has_value()) {
                for(const auto &i : watchers.value()) {
                    publish([i, self, subKey, changeType]() {
                        i->childChanged(self, subKey, changeType);
                    });
                }
            }
        }
        std::shared_ptr<Topics> parent{_parent.lock()};
        // Follow notification chain across all parents
        while(parent && parentNeedsToKnow()) {
            watchers = parent->filterWatchers(WhatHappened::childChanged);
            if(watchers.has_value()) {
                for(const auto &i : watchers.value()) {
                    publish([i, self, subKey, changeType]() {
                        i->childChanged(self, subKey, changeType);
                    });
                }
            }
            parent = parent->getParent();
        }
    }

    void Topics::notifyChange(WhatHappened changeType) {
        notifyChange({}, changeType);
    }

    void Topics::remove(const Timestamp &timestamp) {
        std::unique_lock guard{_mutex};
        if(timestamp < _modtime) {
            return;
        }
        _modtime = timestamp;
        guard.unlock();
        remove();
    }

    void Topics::remove() {
        std::shared_ptr parent(_parent);
        parent->removeChild(*this);
    }

    void Topics::removeChild(ConfigNode &node) {
        // Note, it's important that this is entered via child remove()
        data::StringOrd key = TopicElement::getKey(_environment, node.getNameOrd());
        std::shared_lock guard{_mutex};
        _children.erase(key);
        notifyChange(node.getNameOrd(), WhatHappened::childRemoved);
    }

    data::StringOrd Topics::getNameOrd() const {
        std::shared_lock guard{_mutex};
        return _nameOrd;
    }

    Timestamp Topics::getModTime() const {
        std::shared_lock guard{_mutex};
        return _modtime;
    }

    std::shared_ptr<Topics> Topics::getParent() {
        return _parent.lock();
    }

    bool Topics::excludeTlog() const {
        // cannot use _mutex, uses atomic instead
        return _excludeTlog;
    }

    void Topics::publish(PublishAction action) {
        _environment.configManager.publishQueue().publish(std::move(action));
    }

    TopicElement::TopicElement(const Topic &topic)
        : TopicElement(topic.getNameOrd(), topic.getModTime(), topic._value) {
    }

    Topic &Topic::dflt(data::ValueType defVal) {
        if(isNull()) {
            withNewerValue(Timestamp::never(), std::move(defVal), true);
        }
        return *this;
    }

    Topic &Topic::withNewerValue(
        const config::Timestamp &proposedModTime,
        data::ValueType proposed,
        bool allowTimestampToDecrease,
        bool allowTimestampToIncreaseWhenValueHasntChanged
    ) {
        // Logic tracks that in GG-Java
        data::ValueType currentValue = _value;
        data::ValueType newValue = std::move(proposed);
        Timestamp currentModTime = _modtime;
        bool timestampWouldIncrease =
            allowTimestampToIncreaseWhenValueHasntChanged && proposedModTime > currentModTime;

        // Per GG-Java...
        // If the value hasn't changed, or if the proposed timestamp is in the past
        // AND we don't want to decrease the timestamp AND the timestamp would not
        // increase THEN, return immediately and do nothing.
        if((currentValue == newValue
            || !allowTimestampToDecrease && (proposedModTime < currentModTime))
           && !timestampWouldIncrease) {
            return *this;
        }
        std::optional<data::ValueType> validated =
            _parent->validate(_nameOrd, newValue, currentValue);
        if(validated.has_value()) {
            newValue = validated.value();
        }
        bool changed = true;
        if(newValue == currentValue) {
            changed = false;
            if(!timestampWouldIncrease) {
                return *this;
            }
        }

        _value = newValue;
        _modtime = proposedModTime;
        _parent->updateChild(*this);
        if(changed) {
            _parent->notifyChange(_nameOrd, WhatHappened::changed);
        } else {
            _parent->notifyChange(_nameOrd, WhatHappened::timestampUpdated);
        }
        return *this;
    }

    Topic &Topic::withNewerModTime(const config::Timestamp &newModTime) {
        Timestamp currentModTime = _modtime;
        if(newModTime > currentModTime) {
            _modtime = newModTime;
            _parent->updateChild(*this);
            // GG-Interop: This notification seems to be missed?
            _parent->notifyChange(_nameOrd, WhatHappened::timestampUpdated);
        }
        return *this;
    }

    void Topic::remove(const Timestamp &timestamp) {
        if(timestamp < _modtime) {
            return;
        }
        _modtime = timestamp;
        _parent->updateChild(*this);
        remove();
    }

    void Topic::remove() {
        _parent->removeChild(*this);
    }

    std::string Topic::getName() const {
        return _environment->stringTable.getString(_nameOrd);
    }

    std::vector<std::string> Topic::getKeyPath() const {
        std::vector<std::string> path = _parent->getKeyPath();
        path.push_back(getName());
        return path;
    }

    bool Topic::excludeTlog() const {
        if(_parent->excludeTlog()) {
            return true;
        }
        return util::startsWith(getName(), "_");
    }

    Manager &Manager::read(const std::filesystem::path &path) {
        std::string ext = util::lower(path.extension().generic_string());
        auto timestamp = Timestamp::ofFile(std::filesystem::last_write_time(path));

        if(ext == ".yaml" || ext == ".yml") {
            YamlReader reader{_environment, _root, timestamp};
            reader.read(path);
        } else if(ext == ".tlog" || ext == ".tlog~") {
            TlogReader::mergeTlogInto(_environment, _root, path, false);
        } else if(ext == ".json") {
            throw std::runtime_error("Json config type not yet implemented");
        } else {
            throw std::runtime_error(std::string("Unsupported extension type: ") + ext);
        }
        return *this;
    }

} // namespace config
