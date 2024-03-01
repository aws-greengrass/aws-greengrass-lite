#include "handle_table.hpp"
#include "errors/errors.hpp"
#include "scope/context_full.hpp"

namespace data {
    /**
     * Retrieve object from handle, with null pointer if handle is invalid or does not exist.
     * @param handle Handle of object to retrieve
     * @return Shared pointer ref-copy of object
     */
    std::shared_ptr<TrackedObject> HandleTable::tryGet(const ObjHandle &handle) const {
        std::shared_lock guard{_mutex};
        ObjHandle::Partial p = partial(handle);
        auto pHandleData = _handles.lookup(indexOf(p));
        if(!pHandleData) {
            return {};
        }
        return pHandleData->obj;
    }

    /**
     * Retrieve object from handle, with exception if handle is invalid or does not exist.
     * @param handle Handle of object to retrieve
     * @return Shared pointer ref-copy of object
     */
    std::shared_ptr<TrackedObject> HandleTable::get(const ObjHandle &handle) const {
        if(handle.isNull()) {
            throw errors::NullHandleError();
        }
        auto obj = tryGet(handle);
        if(obj) {
            return obj;
        } else {
            throw errors::InvalidHandleError();
        }
    }

    /**
     * Create a new root for handle tracking.
     * @return New root
     */
    RootHandle HandleTable::createRoot() {

        std::unique_lock guard{_mutex};
        auto &newData = _roots.alloc();
        return applyUncheckedRoot(handleOf(newData.check));
    }

    /**
     * Create a new handle for the object, that will be passed to a plugin. It's tracked (accounted)
     * against a given root. If that root goes away, all handles connected will be released.
     */
    ObjHandle HandleTable::create(
        const std::shared_ptr<TrackedObject> &obj, const RootHandle &root) {

        std::unique_lock guard{_mutex};

        auto pRootData = _roots.lookup(indexOf(root.partial()));
        if(!pRootData) {
            throw std::logic_error("Root handle not found");
        }
        auto &newData = _handles.alloc();
        _handles.insertLast(pRootData->handles, newData.check);
        newData.rootIndex = pRootData->check;
        newData.obj = obj;
        return applyUnchecked(handleOf(newData.check));
    }

    ObjHandle HandleTable::apply(ObjHandle::Partial h) const {
        if(!h) {
            return {};
        }
        check(h);
        return applyUnchecked(h);
    }

    /**
     * When a handle is released by a plugin, it needs to be unlinked.
     */
    bool HandleTable::release(const ObjHandle &handle) noexcept {
        ObjHandle::Partial p = partial(handle);
        std::shared_ptr<TrackedObject> obj;
        std::unique_lock guard{_mutex};
        auto handleIndex = indexOf(p);
        auto pHandleData = _handles.lookup(handleIndex);
        if(!pHandleData) {
            return false; // Did not actually free
        }

        // Don't free yet, if we error out we can be in a bad state
        auto rootIndex = pHandleData->rootIndex;
        auto pRootData = _roots.lookup(rootIndex);

        // Capture object and clean up
        obj = pHandleData->obj;
        pHandleData->obj.reset();
        pHandleData->rootIndex = 0;

        if(pRootData) {
            _handles.unlink(pRootData->handles, handleIndex);
        }
        bool b = _handles.free(handleIndex);
        guard.unlock();
        obj.reset();
        return b;
    }

    /**
     * Release all handles associated with an allocated root.
     */
    bool HandleTable::releaseRoot(data::RootHandle &handle) noexcept {
        assert(this == &handle.table());
        RootHandle::Partial p = handle.detach();
        std::vector<std::shared_ptr<TrackedObject>> objs;
        std::unique_lock guard{_mutex};
        auto handleIndex = indexOf(p);

        auto pRootData = _roots.lookup(handleIndex);
        if(!pRootData) {
            return false; // Did not actually free
        }
        while(pRootData->handles.next != handleImpl::INVALID_INDEX) {
            auto pData = _handles.at(pRootData->handles.next);
            // Take care to not release objects yet
            objs.emplace_back(std::move(pData->obj));
            pData->obj.reset();
            // Remove from root linked list
            pData->rootIndex = handleImpl::INVALID_INDEX;
            _handles.unlink(pRootData->handles, pData->check);
            // Add to free linked list
            _handles.free(pData->check);
        }
        guard.unlock();
        objs.clear(); // this releases references on all objects
        return true;
    }

    /**
     * Throw exception if partial handle is invalid.
     */
    void HandleTable::check(const ObjHandle::Partial handle) const {
        if(handle.isNull()) {
            throw errors::NullHandleError();
        }
        if(!isObjHandleValid(handle)) {
            throw errors::InvalidHandleError();
        }
    }

    /**
     * Return false if partial handle is invalid.
     */
    bool HandleTable::isObjHandleValid(const ObjHandle::Partial handle) const {
        uint32_t index = indexOf(handle);
        std::shared_lock guard{_mutex};
        return _handles.check(index);
    }
} // namespace data
