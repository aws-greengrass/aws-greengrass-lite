#include "shared_struct.hpp"
#include "environment.hpp"
#include "safe_handle.hpp"

namespace data {

    void SharedStruct::rootsCheck(const ContainerModelBase *target
    ) const { // NOLINT(*-no-recursion)
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
        std::shared_ptr<SharedStruct> newCopy{std::make_shared<SharedStruct>(_environment)};
        std::shared_lock guard{_mutex}; // for source
        newCopy->_elements = _elements; // shallow copy
        return newCopy;
    }

    void SharedStruct::putImpl(const StringOrd handle, const StructElement &element) {
        checkedPut(element, [this, handle](auto &el) {
            std::unique_lock guard{_mutex};
            _elements.emplace(handle, el);
        });
    }

    bool SharedStruct::hasKeyImpl(const StringOrd handle) const {
        //_environment.stringTable.assertStringHandle(handle);
        std::shared_lock guard{_mutex};
        auto i = _elements.find(handle);
        return i != _elements.end();
    }

    std::vector<data::StringOrd> SharedStruct::getKeys() const {
        std::vector<data::StringOrd> keys;
        std::shared_lock guard{_mutex};
        keys.reserve(_elements.size());
        for(const auto &_element : _elements) {
            keys.emplace_back(_element.first);
        }
        return keys;
    }

    uint32_t SharedStruct::size() const {
        //_environment.stringTable.assertStringHandle(handle);
        std::shared_lock guard{_mutex};
        return _elements.size();
    }

    StructElement SharedStruct::getImpl(StringOrd handle) const {
        //_environment.stringTable.assertStringHandle(handle);
        std::shared_lock guard{_mutex};
        auto i = _elements.find(handle);
        if(i == _elements.end()) {
            return {};
        } else {
            return i->second;
        }
    }
} // namespace data
