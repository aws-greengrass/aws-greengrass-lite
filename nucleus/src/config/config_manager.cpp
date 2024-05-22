#include "config/config_manager.hpp"
#include "config/config_nodes.hpp"
#include "config/config_timestamp.hpp"
#include "config/update_behavior_tree.hpp"
#include "config/yaml_config.hpp"
#include "transaction_log.hpp"
#include <unordered_map>
#include <util.hpp>
#include <utility>

//
// Note that config intake is case-insensitive - config comes from
// a settings file (YAML), transaction log (YAML), or cloud (JSON or YAML)
// For optimization, this implementation assumes all config keys are stored
// lower-case which means translation on intake is important
//
namespace config {

    data::Symbol TopicElement::getKey() const {
        return getKey(_name);
    }

    data::Symbol TopicElement::getKey(data::Symbol key) {
        if(!key) {
            return key;
        }
        std::string str = key.toString();
        // a folded string strictly acts on the ascii range and not on international
        // characters, this keeps it predictable and handles the problems with GG
        // configs
        std::string lowered = util::lower(str);
        if(str == lowered) {
            return key;
        } else {
            return key.table().intern(lowered);
        }
    }

    Topics::Topics(
        const scope::UsingContext &context,
        const std::shared_ptr<Topics> &parent,
        const data::Symbol &key,
        const Timestamp &modtime)
        : data::StructModelBase(context), _nameOrd(key), _modtime(modtime), _parent(parent) {
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
        return _nameOrd.toString();
    }

    std::string Topics::getName() const {
        std::shared_lock guard{_mutex};
        return getNameUnsafe();
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void Topics::updateFromMap(
        const TopicElement &mapElement, const std::shared_ptr<UpdateBehaviorTree> &mergeBehavior) {
        if(mapElement.empty() || mapElement.isNull()
           || !mapElement.isType<data::StructModelBase>()) {
            return;
        }
        std::shared_ptr<data::StructModelBase> map;
        if(map = mapElement.getStruct(); map == nullptr) {
            return;
        }

        std::unordered_map<std::string, data::Symbol> childrenToRemove;
        auto ctx = context();
        auto &syms = ctx->symbols();

        for(const auto &i : _children.get()) {
            auto sym = syms.apply(i.first);
            childrenToRemove.insert({sym.toString(), sym});
        }

        for(const auto &key : map->getKeys()) {
            auto value = map->get(key);

            childrenToRemove.erase(key);
            updateChild(TopicElement{key, Timestamp::never(), value});
        }

        // if nullptr, means not REPLACE object, can skip removal
        if(!mergeBehavior) {
            return;
        }

        for(auto &[_, childSym] : childrenToRemove) {
            auto childMergeBehavior = mergeBehavior->getChildBehavior(childSym);

            // remove the existing child if its merge behavior is REPLACE
            if(std::dynamic_pointer_cast<ReplaceBehaviorTree>(childMergeBehavior)) {
                removeChild(*getNode(childSym));
            }
        }
    }

    void Topics::updateChild(const Topic &element) {
        updateChild(TopicElement(element));
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void Topics::updateChild(const TopicElement &element) {
        data::Symbol key = element.getKey();
        if(element.isType<data::StructModelBase>()) {
            auto newNode = createInteriorChild(key);
            newNode->updateFromMap(element);
            return;
        }
        checkedPut(element, [this, key, &element](auto &el) {
            std::unique_lock guard{_mutex};
            _children.insert_or_assign(key, element);
        });
    }

    void Topics::rootsCheck(
        const data::ContainerModelBase *target) const { // NOLINT(*-no-recursion)
        if(this == target) {
            throw std::runtime_error("Recursive reference of structure");
        }
        // we don't want to keep nesting locks else we will deadlock
        std::shared_lock guard{_mutex};
        std::vector<std::shared_ptr<data::ContainerModelBase>> structs;
        for(const auto &i : _children.get()) {
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
        data::Symbol subKey, const std::shared_ptr<Watcher> &watcher, WhatHappened reasons) {
        if(!watcher) {
            return; // null watcher is a no-op
        }
        data::Symbol normKey = TopicElement::getKey(subKey);
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
        const std::shared_ptr<Watcher> &watcher, config::WhatHappened reasons) {
        _parent->addWatcher(_name, watcher, reasons);
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
        config::WhatHappened reasons) const {
        return filterWatchers({}, reasons);
    }

    std::optional<std::vector<std::shared_ptr<Watcher>>> Topics::filterWatchers(
        data::Symbol key, config::WhatHappened reasons) const {
        if(!hasWatchers()) {
            return {};
        }
        data::Symbol normKey = TopicElement::getKey(key);
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
            std::make_shared<Topics>(context(), parent, _nameOrd, _modtime)};
        for(const auto &i : _children.get()) {
            newCopy->put(_children.apply(i.first), i.second);
        }
        return newCopy;
    }

    std::shared_ptr<data::StructModelBase> Topics::createForChild() {
        const std::shared_ptr<Topics> parent{ref<Topics>()};
        std::shared_lock guard{_mutex}; // for source
        std::shared_ptr<Topics> newChild{
            std::make_shared<Topics>(context(), parent, _nameOrd, _modtime)};
        return newChild;
    }

    void Topics::putImpl(const data::Symbol handle, const data::StructElement &element) {
        updateChild(TopicElement{handle, Timestamp::never(), element});
    }

    bool Topics::hasKeyImpl(const data::Symbol handle) const {
        //_environment.stringTable.assertStringHandle(handle);
        data::Symbol key = TopicElement::getKey(handle);
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

    std::vector<data::Symbol> Topics::getKeys() const {
        std::vector<data::Symbol> keys;
        std::shared_lock guard{_mutex};
        keys.reserve(_children.size());
        for(const auto &_element : _children) {
            keys.emplace_back(context()->symbols().apply(_element.first));
        }
        return keys;
    }

    std::shared_ptr<data::ListModelBase> Topics::getKeysAsList() const {
        auto keys{std::make_shared<data::SharedList>(context())};
        std::shared_lock guard{_mutex};
        keys->reserve(_children.size());
        auto ctx = context();
        auto &syms = ctx->symbols();
        for(const auto &_element : _children) {
            keys->push(syms.apply(_element.first));
        }
        return keys;
    }

    uint32_t Topics::size() const {
        std::shared_lock guard{_mutex};
        return _children.size();
    }

    bool Topics::empty() const {
        std::shared_lock guard{_mutex};
        return _children.empty();
    }

    TopicElement Topics::createChild(
        data::Symbol nameOrd, const std::function<TopicElement(data::Symbol)> &creator) {
        data::Symbol key = TopicElement::getKey(nameOrd);
        std::unique_lock guard{_mutex};
        auto i = _children.find(key);
        if(i != _children.end()) {
            return i->second;
        } else {
            TopicElement element = creator(key);
            _children.emplace(key, element);
            return element;
        }
    }

    std::shared_ptr<Topics> Topics::createInteriorChild(
        data::Symbol nameOrd, const Timestamp &timestamp) {
        TopicElement leaf = createChild(nameOrd, [this, &timestamp, nameOrd](auto ord) {
            std::shared_ptr<Topics> parent{ref<Topics>()};
            std::shared_ptr<Topics> nested{
                std::make_shared<Topics>(context(), parent, nameOrd, timestamp)};
            // Note: Time on TopicElement is ignored for interior children - this is intentional
            return TopicElement(ord, Timestamp::never(), data::ValueType(nested));
        });
        return leaf.castObject<Topics>();
    }

    std::shared_ptr<Topics> Topics::createInteriorChild(
        std::string_view name, const Timestamp &timestamp) {
        data::Symbol handle = context()->symbols().intern(name);
        return createInteriorChild(handle, timestamp);
    }

    Topic Topics::lookup(const std::vector<std::string> &path) {
        std::shared_ptr<Topics> node{ref<Topics>()};
        auto steps = path.size();
        auto it = path.begin();
        while(--steps > 0) {
            node = node->createInteriorChild(*it);
            ++it;
        }
        return node->createTopic(*it);
    }

    Topic Topics::lookup(Timestamp timestamp, const std::vector<std::string> &path) {
        std::shared_ptr<Topics> node{ref<Topics>()};
        auto steps = path.size();
        auto it = path.begin();
        while(--steps > 0) {
            node = node->createInteriorChild(*it, timestamp);
            ++it;
        }
        return node->createTopic(*it, timestamp);
    }

    std::shared_ptr<Topics> Topics::lookupTopics(const std::vector<std::string> &path) {
        return lookupTopics(Timestamp::now(), path);
    }

    std::shared_ptr<Topics> Topics::lookupTopics(
        Timestamp timestamp, const std::vector<std::string> &path) {
        std::shared_ptr<Topics> node{ref<Topics>()};
        for(const auto &p : path) {
            node = node->createInteriorChild(p, timestamp);
        }
        return node;
    }

    std::optional<Topic> Topics::find(std::initializer_list<std::string> path) {
        std::shared_ptr<Topics> _node = ref<Topics>();
        if(path.size() == 0) {
            throw std::runtime_error("Empty path provided");
        }
        auto steps = path.size();
        auto it = path.begin();
        while(--steps > 0) {
            _node = _node->findInteriorChild(*it);
            ++it;
        }
        if(!_node) {
            return {};
        }
        return _node->getTopic(*it);
    }

    data::ValueType Topics::findOrDefault(
        const data::ValueType &defaultV, std::initializer_list<std::string> path) {
        std::optional<config::Topic> potentialTopic = find(path);
        if(potentialTopic.has_value()) {
            return potentialTopic->get();
        }
        return defaultV;
    }

    std::shared_ptr<config::Topics> Topics::findTopics(std::initializer_list<std::string> path) {
        std::shared_ptr<Topics> _node = ref<Topics>();
        for(const auto &it : path) {
            _node = _node->findInteriorChild(it);
        }
        return _node;
    }

    std::shared_ptr<Topics> Topics::findInteriorChild(data::Symbol handle) {
        data::Symbol key = TopicElement::getKey(handle);
        std::shared_lock guard{_mutex};
        auto i = _children.find(key);
        if(i != _children.end()) {
            return std::dynamic_pointer_cast<Topics>(getNode(handle));
        } else {
            return nullptr;
        }
    }

    std::shared_ptr<Topics> Topics::findInteriorChild(std::string_view name) {
        data::Symbol handle = context()->symbols().intern(name);
        return findInteriorChild(handle);
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
                leafs.emplace_back(context(), self, i.second);
            }
        }
        return leafs;
    }

    Topic Topics::createTopic(data::Symbol nameOrd, const Timestamp &timestamp) {
        TopicElement el = createChild(
            nameOrd, [&](auto ord) { return TopicElement(ord, timestamp, data::ValueType{}); });
        return Topic(context(), ref<Topics>(), el);
    }

    Topic Topics::createTopic(std::string_view name, const Timestamp &timestamp) {
        data::Symbol handle = context()->symbols().intern(name);
        return createTopic(handle, timestamp);
    }

    Topic Topics::getTopic(data::Symbol handle) {
        data::Symbol key = TopicElement::getKey(handle);
        std::shared_lock guard{_mutex};
        auto i = _children.find(key);
        if(i != _children.end()) {
            return Topic(context(), ref<Topics>(), i->second);
        } else {
            return Topic(context(), nullptr, {});
        }
    }

    Topic Topics::getTopic(std::string_view name) {
        data::Symbol handle = context()->symbols().intern(name);
        return getTopic(handle);
    }

    data::StructElement Topics::getImpl(data::Symbol handle) const {
        // needed for base class
        data::Symbol key = TopicElement::getKey(handle);
        std::shared_lock guard{_mutex};
        auto i = _children.find(key);
        if(i != _children.end()) {
            return i->second.slice();
        } else {
            return {};
        }
    }

    std::shared_ptr<ConfigNode> Topics::getNode(data::Symbol handle) {
        data::Symbol key = TopicElement::getKey(handle);
        std::shared_lock guard{_mutex};
        auto i = _children.find(key);
        if(i != _children.end()) {
            if(i->second.isType<Topics>()) {
                return i->second.castObject<Topics>();
            } else {
                return std::make_shared<Topic>(context(), ref<Topics>(), i->second);
            }
        } else {
            return {};
        }
    }

    std::shared_ptr<ConfigNode> Topics::getNode(std::string_view name) {
        data::Symbol handle = context()->symbols().intern(name);
        return getNode(handle);
    }

    std::shared_ptr<ConfigNode> Topics::getNode(const std::vector<std::string> &path) {
        if(path.empty()) {
            throw std::runtime_error("Empty path provided");
        }
        std::shared_ptr<ConfigNode> node{ref<Topics>()};
        for(const auto &it : path) {
            std::shared_ptr<Topics> t{std::dynamic_pointer_cast<Topics>(node)};
            if(!t) {
                return {};
            }
            node = t->getNode(it);
        }
        return node;
    }

    std::optional<data::ValueType> Topics::validate(
        data::Symbol subKey, const data::ValueType &proposed, const data::ValueType &currentValue) {
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

    void Topics::notifyChange(data::Symbol subKey, WhatHappened changeType) {
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
        data::Symbol key = TopicElement::getKey(node.getNameOrd());
        std::shared_lock guard{_mutex};
        _children.erase(key);
        notifyChange(node.getNameOrd(), WhatHappened::childRemoved);
    }

    data::Symbol Topics::getNameOrd() const {
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

    void Topics::publish(const PublishAction &action) {
        context()->configManager().publishQueue().publish(action);
    }

    data::Symbol Topics::foldKey(const data::Symbolish &key, bool ignoreCase) const {
        return key; // Case is already ignored
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
        bool allowTimestampToIncreaseWhenValueHasntChanged) {
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
            || (!allowTimestampToDecrease && (proposedModTime < currentModTime)))
           && !timestampWouldIncrease) {
            return *this;
        }
        std::optional<data::ValueType> validated = _parent->validate(_name, newValue, currentValue);
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
            _parent->notifyChange(_name, WhatHappened::changed);
        } else {
            _parent->notifyChange(_name, WhatHappened::timestampUpdated);
        }
        return *this;
    }

    Topic &Topic::withNewerModTime(const config::Timestamp &newModTime) {
        Timestamp currentModTime = _modtime;
        if(newModTime > currentModTime) {
            _modtime = newModTime;
            _parent->updateChild(*this);
            // GG-Interop: This notification seems to be missed?
            _parent->notifyChange(_name, WhatHappened::timestampUpdated);
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
        return _name.toString();
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
            YamlConfigReader reader{context(), _root, timestamp};
            reader.read(path);
        } else if(ext == ".tlog" || ext == ".tlog~") {
            TlogReader::mergeTlogInto(context(), _root, path, false);
        } else if(ext == ".json") {
            throw std::runtime_error("Json config type not yet implemented");
        } else {
            throw std::runtime_error(std::string("Unsupported extension type: ") + ext);
        }
        return *this;
    }
    Manager::Manager(const scope::UsingContext &context)
        : scope::UsesContext(context),
          _root{std::make_shared<Topics>(context, nullptr, data::Symbol{}, Timestamp::never())},
          _publishQueue(context) {
    }

    Topic Manager::lookup(std::initializer_list<std::string> path) {
        return _root->lookup(path);
    }
    Topic Manager::lookup(Timestamp timestamp, std::initializer_list<std::string> path) {
        return _root->lookup(timestamp, path);
    }
    std::shared_ptr<Topics> Manager::lookupTopics(std::initializer_list<std::string> path) {
        return _root->lookupTopics(path);
    }
    std::shared_ptr<Topics> Manager::lookupTopics(
        Timestamp timestamp, std::initializer_list<std::string> path) {
        return _root->lookupTopics(timestamp, path);
    }
    std::optional<Topic> Manager::find(std::initializer_list<std::string> path) {
        return _root->find(path);
    }
    data::ValueType Manager::findOrDefault(
        const data::ValueType &defaultV, std::initializer_list<std::string> path) {
        return _root->findOrDefault(defaultV, path);
    }
    std::shared_ptr<config::Topics> Manager::findTopics(std::initializer_list<std::string> path) {
        return _root->findTopics(path);
    }
    void Manager::mergeMap(const Timestamp &timestamp, const TopicElement &mapElement) {
        std::shared_ptr<MergeBehaviorTree> mergeBehavior{
            std::make_shared<MergeBehaviorTree>(context(), timestamp)};
        this->updateMap(mapElement, mergeBehavior);
    }
    void Manager::updateMap(
        const TopicElement &mapElement, const std::shared_ptr<UpdateBehaviorTree> &updateBehavior) {
        _configUnderUpdate.store(true);
        // TODO: determine if mutex and notification is needed here.
        // Needed for lifecycle state change config waiting.
        publishQueue().publish([this, mapElement, updateBehavior]() {
            _root->updateFromMap(mapElement, updateBehavior);
            _configUnderUpdate.store(false);
        });
    }
} // namespace config
