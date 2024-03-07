#include "shared_struct.hpp"
#include "safe_handle.hpp"
#include "scope/context_full.hpp"
#include "string_table.hpp"
#include <shared_mutex>

namespace data {

    void SharedStruct::rootsCheck(
        const ContainerModelBase *target) const { // NOLINT(*-no-recursion)
        if(this == target) {
            throw std::runtime_error("Recursive reference of container");
        }
        // we don't want to keep nesting locks else we will deadlock
        std::shared_lock guard{_mutex};
        std::vector<std::shared_ptr<ContainerModelBase>> containers;
        for(const auto &i : _elements) {
            if(i.second.isContainer()) {
                std::shared_ptr<ContainerModelBase> otherContainer = i.second.getContainer();
                if(otherContainer) {
                    containers.emplace_back(otherContainer);
                }
            }
        }
        guard.unlock();
        for(const auto &i : containers) {
            i->rootsCheck(target);
        }
    }

    std::shared_ptr<StructModelBase> SharedStruct::copy() const {
        std::shared_ptr<SharedStruct> newCopy{std::make_shared<SharedStruct>(context())};
        std::shared_lock guard{_mutex}; // for source
        newCopy->_elements = _elements; // shallow copy
        return newCopy;
    }

    void SharedStruct::putImpl(const Symbol symbol, const StructElement &element) {
        checkedPut(element, [this, symbol](auto &el) {
            std::unique_lock guard{_mutex};
            _elements.insert_or_assign(symbol, el);
        });
    }

    bool SharedStruct::hasKeyImpl(const Symbol symbol) const {
        //_environment.stringTable.assertStringHandle(handle);
        std::shared_lock guard{_mutex};
        auto i = _elements.find(symbol);
        return i != _elements.end();
    }

    std::vector<data::Symbol> SharedStruct::getKeys() const {
        std::vector<data::Symbol> keys;
        std::shared_lock guard{_mutex};
        keys.reserve(_elements.size());
        auto ctx = context();
        auto &syms = ctx->symbols();
        for(const auto &_element : _elements) {
            keys.emplace_back(syms.apply(_element.first));
        }
        return keys;
    }

    std::shared_ptr<ListModelBase> SharedStruct::getKeysAsList() const {
        auto keys{std::make_shared<SharedList>(context())};
        std::shared_lock guard{_mutex};
        keys->reserve(_elements.size());
        auto ctx = context();
        auto &syms = ctx->symbols();
        for(const auto &_element : _elements) {
            keys->push(syms.apply(_element.first));
        }
        return keys;
    }

    uint32_t SharedStruct::size() const {
        std::shared_lock guard{_mutex};
        return _elements.size();
    }

    bool SharedStruct::empty() const {
        std::shared_lock guard{_mutex};
        return _elements.empty();
    }

    StructElement SharedStruct::getImpl(Symbol symbol) const {
        std::shared_lock guard{_mutex};
        auto i = _elements.find(symbol);
        if(i == _elements.end()) {
            return {};
        } else {
            return i->second;
        }
    }

    Symbol SharedStruct::foldKey(const Symbolish &key, bool ignoreCase) const {
        if(!ignoreCase) {
            return key;
        }
        auto ctx = context();
        auto &syms = ctx->symbols();
        std::shared_lock guard{_mutex};
        if(_elements.find(key) != _elements.end()) {
            return key;
        }
        std::string keyLower = util::lower(key.toString());
        for(const auto &k : _elements) {
            auto symComp = syms.apply(k.first);
            auto keyComp = util::lower(symComp.toString());
            if(keyLower == keyComp) {
                return symComp;
            }
        }
        return key;
    }

    template<>
    StructModelBase *Archive::initSharedPtr(std::shared_ptr<StructModelBase> &ptr) {
        auto newPtr = std::make_shared<SharedStruct>(scope::context());
        ptr = newPtr;
        return newPtr.get();
    }
    template<>
    SharedStruct *Archive::initSharedPtr(std::shared_ptr<SharedStruct> &ptr) {
        auto newPtr = std::make_shared<SharedStruct>(scope::context());
        ptr = newPtr;
        return newPtr.get();
    }

} // namespace data
