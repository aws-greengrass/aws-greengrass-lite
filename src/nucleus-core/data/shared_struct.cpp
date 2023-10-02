#include "shared_struct.h"
#include "environment.h"
#include "safe_handle.h"

namespace data {

    void SharedStruct::rootsCheck(const ContainerModelBase *target) const { // NOLINT(*-no-recursion)
        if (this == target) {
            throw std::runtime_error("Recursive reference of container");
        }
        // we don't want to keep nesting locks else we will deadlock
        std::shared_lock guard{_mutex};
        std::vector<std::shared_ptr<ContainerModelBase>> containers;
        for (auto const &i: _elements) {
            if (i.second.isContainer()) {
                std::shared_ptr<ContainerModelBase> otherContainer = i.second.getContainer();
                if (otherContainer) {
                    containers.emplace_back(otherContainer);
                }
            }
        }
        guard.unlock();
        for (auto const &i: containers) {
            i->rootsCheck(target);
        }
    }

    std::shared_ptr<StructModelBase> SharedStruct::copy() const {
        std::shared_ptr<SharedStruct> newCopy{std::make_shared<SharedStruct>(_environment)};
        std::shared_lock guard{_mutex}; // for source
        newCopy->_elements = _elements; // shallow copy
        return newCopy;
    }

    void SharedStruct::put(const StringOrd handle, const StructElement &element) {
        checkedPut(element, [this, handle](auto &el) {
            std::unique_lock guard{_mutex};
            _elements.emplace(handle, el);
        });
    }

    void SharedStruct::put(const std::string_view sv, const StructElement &element) {
        Handle handle = _environment.stringTable.getOrCreateOrd(std::string(sv));
        put(handle, element);
    }

    bool SharedStruct::hasKey(const StringOrd handle) const {
        //_environment.stringTable.assertStringHandle(handle);
        std::shared_lock guard{_mutex};
        auto i = _elements.find(handle);
        return i != _elements.end();
    }

    uint32_t SharedStruct::size() const {
        //_environment.stringTable.assertStringHandle(handle);
        std::shared_lock guard{_mutex};
        return _elements.size();
    }

    StructElement SharedStruct::get(const std::string_view sv) const {
        Handle handle = _environment.stringTable.getOrCreateOrd(std::string(sv));
        return get(handle);
    }

    StructElement SharedStruct::get(StringOrd handle) const {
        //_environment.stringTable.assertStringHandle(handle);
        std::shared_lock guard{_mutex};
        auto i = _elements.find(handle);
        if (i == _elements.end()) {
            return {};
        } else {
            return i->second;
        }
    }
}
