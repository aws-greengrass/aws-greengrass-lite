#include "config_manager.h"
#include "data/environment.h"
#include "util.h"
#include "yaml_helper.h"
#include <utility>

//
// Note that config intake is case insensitive - config comes from
// a settings file (YAML), transaction log (YAML), or cloud (JSON or YAML)
// For optimization, this implementation assumes all config keys are stored
// lower-case which means translation on intake is important
//
namespace config {

    data::StringOrd Element::getKey(data::Environment &env) const {
        return getKey(env, _nameOrd);
    }

    data::StringOrd Element::getKey(data::Environment &env, data::StringOrd nameOrd) {
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

    Element &Element::setName(data::Environment &env, const std::string &str) {
        return setOrd(env.stringTable.getOrCreateOrd(str));
    }

    Topics::Topics(
        data::Environment &environment,
        const std::shared_ptr<Topics> &parent,
        const data::StringOrd &key
    )
        : data::StructModelBase{environment}, _parent{parent}, _key{key} {
        if(parent && parent->excludeTlog() || util::startsWith(getKey(), "_")) {
            _excludeTlog = true;
        }
    }

    std::string Topics::getKey() const {
        std::shared_lock guard{_mutex};
        return _environment.stringTable.getString(_key);
    }

    void Topics::updateChild(const Element &element) {
        data::StringOrd key = element.getKey(_environment);
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
        data::StringOrd normKey = Element::getKey(_environment, subKey);
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

    Topic &
        Topic::addWatcher(const std::shared_ptr<Watcher> &watcher, config::WhatHappened reasons) {
        _parent->addWatcher(_value.getNameOrd(), watcher, reasons);
        return *this;
    }

    bool Topics::hasWatchers() const {
        std::shared_lock guard{_mutex};
        return !_watching.empty();
    }

    std::optional<std::vector<std::shared_ptr<Watcher>>>
        Topics::filterWatchers(config::WhatHappened reasons) const {
        return filterWatchers({}, reasons);
    }

    std::optional<std::vector<std::shared_ptr<Watcher>>>
        Topics::filterWatchers(data::StringOrd key, config::WhatHappened reasons) const {
        if(!hasWatchers()) {
            return {};
        }
        data::StringOrd normKey = Element::getKey(_environment, key);
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
        std::shared_ptr<Topics> newCopy{std::make_shared<Topics>(_environment, parent, _key)};
        for(const auto &i : _children) {
            newCopy->put(i.first, i.second);
        }
        return newCopy;
    }

    void Topics::put(const data::StringOrd handle, const data::StructElement &element) {
        updateChild(Element{handle, element});
    }

    void Topics::put(const std::string_view sv, const data::StructElement &element) {
        data::StringOrd handle = _environment.stringTable.getOrCreateOrd(std::string(sv));
        put(handle, element);
    }

    bool Topics::hasKey(const data::StringOrd handle) const {
        //_environment.stringTable.assertStringHandle(handle);
        data::StringOrd key = Element::getKey(_environment, handle);
        std::shared_lock guard{_mutex};
        auto i = _children.find(key);
        return i != _children.end();
    }

    std::vector<std::string> Topics::getKeyPath() const { // NOLINT(*-no-recursion)
        std::shared_lock guard{_mutex};
        std::shared_ptr<Topics> parent{_parent.lock()};
        if(parent) {
            std::vector<std::string> path = parent->getKeyPath();
            path.push_back(getKey());
            return path;
        } else {
            return {getKey()};
        }
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

    Element Topics::createChild(
        data::StringOrd nameOrd, const std::function<Element(data::StringOrd)> &creator
    ) {
        data::StringOrd key = Element::getKey(_environment, nameOrd);
        std::unique_lock guard{_mutex};
        auto i = _children.find(key);
        if(i != _children.end()) {
            return i->second;
        } else {
            return _children[key] = creator(nameOrd);
        }
    }

    std::shared_ptr<Topics>
        Topics::createInteriorChild(data::StringOrd nameOrd, const Timestamp &timestamp) {
        Element leaf = createChild(nameOrd, [this, &timestamp, nameOrd](auto ord) {
            std::shared_ptr<Topics> parent{ref<Topics>()};
            std::shared_ptr<Topics> nested{std::make_shared<Topics>(_environment, parent, nameOrd)};
            return Element(ord, timestamp, nested);
        });
        return leaf.getTopicsRef();
    }

    std::shared_ptr<Topics>
        Topics::createInteriorChild(std::string_view sv, const Timestamp &timestamp) {
        data::StringOrd handle = _environment.stringTable.getOrCreateOrd(std::string(sv));
        return createInteriorChild(handle, timestamp);
    }

    std::vector<std::shared_ptr<Topics>> Topics::getInteriors() {
        std::vector<std::shared_ptr<Topics>> interiors;
        std::shared_lock guard{_mutex};
        for(const auto &i : _children) {
            if(i.second.isTopics()) {
                interiors.push_back(i.second.getTopicsRef());
            }
        }
        return interiors;
    }

    std::vector<Topic> Topics::getLeafs() {
        std::shared_ptr<Topics> self = ref<Topics>();
        std::vector<Topic> leafs;
        std::shared_lock guard{_mutex};
        for(const auto &i : _children) {
            if(!i.second.isTopics()) {
                leafs.emplace_back(_environment, self, i.second);
            }
        }
        return leafs;
    }

    Topic Topics::createChild(data::StringOrd nameOrd, const Timestamp &timestamp) {
        Element el = createChild(nameOrd, [&](auto ord) { return Element(ord, timestamp); });
        return Topic(_environment, ref<Topics>(), std::move(el));
    }

    Topic Topics::createChild(std::string_view sv, const Timestamp &timestamp) {
        data::StringOrd handle = _environment.stringTable.getOrCreateOrd(std::string(sv));
        return createChild(handle, timestamp);
    }

    Element Topics::getChildElement(data::StringOrd handle) const {
        data::StringOrd key = Element::getKey(_environment, handle);
        std::shared_lock guard{_mutex};
        auto i = _children.find(key);
        if(i != _children.end()) {
            return i->second;
        } else {
            return {};
        }
    }

    Element Topics::getChildElement(std::string_view sv) const {
        data::StringOrd handle = _environment.stringTable.getOrCreateOrd(std::string(sv));
        return getChildElement(handle);
    }

    Topic Topics::getChild(data::StringOrd handle) {
        Element el = getChildElement(handle);
        return Topic(_environment, ref<Topics>(), el);
    }

    Topic Topics::getChild(std::string_view sv) {
        data::StringOrd handle = _environment.stringTable.getOrCreateOrd(std::string(sv));
        return getChild(handle);
    }

    data::StructElement Topics::get(data::StringOrd handle) const {
        return getChildElement(handle).slice();
    }

    data::StructElement Topics::get(const std::string_view sv) const {
        data::StringOrd handle = _environment.stringTable.getOrCreateOrd(std::string(sv));
        return get(handle);
    }

    size_t Topics::getSize() const {
        std::shared_lock guard{_mutex};
        return _children.size();
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
        if(watchers.has_value()) {
            for(const auto &i : watchers.value()) {
                i->changed(ref<Topics>(), subKey, changeType);
            }
        }
        watchers = filterWatchers(WhatHappened::childChanged);
        if(watchers.has_value()) {
            for(const auto &i : watchers.value()) {
                i->childChanged(ref<Topics>(), subKey, changeType);
            }
        }
    }

    void Topics::notifyChange(WhatHappened changeType) {
        auto watchers = filterWatchers(changeType);
        if(watchers.has_value()) {
            for(const auto &i : watchers.value()) {
                i->changed(ref<Topics>(), {}, changeType);
            }
        }
    }

    Topic &Topic::dflt(data::ValueType defVal) {
        if(!_value) {
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
        data::ValueType currentValue = _value.get();
        data::ValueType newValue = std::move(proposed);
        Timestamp currentModTime = _value.getModTime();
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
            _parent->validate(_value.getNameOrd(), newValue, currentValue);
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

        _value.set(newValue);
        _value.setModTime(proposedModTime);
        _parent->updateChild(_value);
        if(changed) {
            _parent->notifyChange(_value.getNameOrd(), WhatHappened::changed);
        } else {
            _parent->notifyChange(_value.getNameOrd(), WhatHappened::timestampUpdated);
        }
        return *this;
    }

    Manager &Manager::read(const std::filesystem::path &path) {
        std::string ext = util::lower(path.extension().generic_string());
        Timestamp timestamp{std::filesystem::last_write_time(path)};

        if(ext == ".yaml" || ext == ".yml") {
            YamlReader reader{_environment, _root, timestamp};
            reader.read(path);
        } else if(ext == ".tlog" || ext == ".tlog~") {
            // config::TlogReader::mergeTLogInto()
            throw std::runtime_error("Tlog config type not yet implemented");
        } else if(ext == ".json") {
            throw std::runtime_error("Json config type not yet implemented");
        } else {
            throw std::runtime_error(std::string("Unsupported extension type: ") + ext);
        }
        return *this;
    }

} // namespace config
