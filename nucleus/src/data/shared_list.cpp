#include "shared_list.hpp"
#include "scope/context_full.hpp"

namespace data {

    void SharedList::rootsCheck(const ContainerModelBase *target) const { // NOLINT(*-no-recursion)
        if(this == target) {
            throw std::runtime_error("Recursive reference of container");
        }
        // we don't want to keep nesting locks else we will deadlock
        std::shared_lock guard{_mutex};
        std::vector<std::shared_ptr<ContainerModelBase>> containers;
        for(const auto &i : _elements) {
            if(i.isContainer()) {
                std::shared_ptr<ContainerModelBase> otherContainer = i.getContainer();
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

    std::shared_ptr<ListModelBase> SharedList::copy() const {
        std::shared_ptr<SharedList> newCopy{std::make_shared<SharedList>(context())};
        std::shared_lock guard{_mutex}; // for source
        newCopy->_elements = _elements; // shallow copy
        return newCopy;
    }

    void SharedList::put(int32_t idx, const StructElement &element) {
        checkedPut(element, [this, idx](auto &el) {
            std::unique_lock guard{_mutex};
            size_t realIdx;
            if(idx < 0) {
                realIdx = _elements.size() + idx;
            } else {
                realIdx = idx;
            }
            if(realIdx > _elements.size()) {
                throw std::out_of_range("Put index out of range");
            } else if(_elements.size() >= MAX_LIST_SIZE) {
                throw std::out_of_range("List too large");
            } else if(realIdx == _elements.size()) {
                _elements.push_back(el);
            } else {
                _elements.at(realIdx) = el;
            }
        });
    }

    void SharedList::push(const StructElement &element) {
        checkedPut(element, [this](auto &el) {
            std::unique_lock guard{_mutex};
            if(_elements.size() >= MAX_LIST_SIZE) {
                throw std::out_of_range("List too large");
            } else {
                _elements.push_back(el);
            }
        });
    }

    void SharedList::insert(int32_t idx, const StructElement &element) {
        checkedPut(element, [this, idx](auto &el) {
            std::unique_lock guard{_mutex};
            size_t realIdx;
            if(idx < 0) {
                realIdx = _elements.size() + idx + 1; // -1 inserts at end
            } else {
                realIdx = idx;
            }
            if(realIdx > _elements.size()) {
                throw std::out_of_range("Put index out of range");
            } else if(_elements.size() >= MAX_LIST_SIZE) {
                throw std::out_of_range("List too large");
            } else if(realIdx == _elements.size()) {
                _elements.push_back(el);
            } else {
                auto it = _elements.begin();
                std::advance(it, realIdx);
                _elements.insert(it, el);
            }
        });
    }

    uint32_t SharedList::size() const {
        std::shared_lock guard{_mutex};
        return _elements.size();
    }

    StructElement SharedList::get(int32_t idx) const {
        std::shared_lock guard{_mutex};
        size_t realIdx;
        if(idx < 0) {
            realIdx = _elements.size() + idx;
        } else {
            realIdx = idx;
        }
        if(realIdx >= _elements.size()) {
            return {};
        } else {
            return _elements.at(realIdx);
        }
    }

    template<>
    ListModelBase *Archive::initSharedPtr(std::shared_ptr<ListModelBase> &ptr) {
        auto newPtr = std::make_shared<SharedList>(scope::context());
        ptr = newPtr;
        return newPtr.get();
    }
    template<>
    SharedList *Archive::initSharedPtr(std::shared_ptr<SharedList> &ptr) {
        auto newPtr = std::make_shared<SharedList>(scope::context());
        ptr = newPtr;
        return newPtr.get();
    }

} // namespace data
