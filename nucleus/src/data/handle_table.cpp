#include "handle_table.hpp"
#include "environment.hpp"

namespace data {
    ObjectAnchor HandleTable::tryGet(ObjHandle handle) {
        std::shared_lock guard{_mutex};
        auto i = _handles.find(handle);
        if(i == _handles.end()) {
            return {};
        }
        return i->second.lock().withHandle(handle);
    }

    ObjectAnchor HandleTable::get(ObjHandle handle) const {
        ObjectAnchor anchor = tryGet(handle);
        if(anchor) {
            return anchor;
        } else {
            throw std::invalid_argument("Object handle is not valid");
        }
    }

    //
    // Create a new handle for the object, using the partial information in the
    // provided ObjectAnchor structure. The handle IDs should appear almost random
    // (yet with consistency run-to-run for debugging) which will allow catching
    // handle ID bugs easier. "Stumbling" on an ID is rare. Stumbling on an ID that
    // matches the same object type is even rarer. Two handles for the same
    // object/owner pair should also result in a different ID to catch any
    // release-then-create bugs. The handle system ensures that if a handle is
    // returned, it points to a real object of the required type.
    //
    ObjectAnchor HandleTable::create(const ObjectAnchor &partial) {
        if(!partial) {
            return {}; // pass through null anchor
        }
        if(partial.getHandle()) {
            return partial; // already created handle
        }
        std::shared_ptr<TrackingScope> scope{partial.getOwner()};
        if(!scope) {
            throw std::runtime_error("create handle with null scope");
        }
        std::unique_lock guard{_mutex};
        std::hash<std::shared_ptr<TrackedObject>> hashFunc;
        // 1/ don't create incrementing numbers - hashing used for this
        // 2/ don't reuse numbers (help catch handle bugs) for the same pair - _salt
        // used for this
        size_t ptrHash =
            hashFunc(partial.getBase()) * PRIME1 + hashFunc(partial.getOwner()) * PRIME2;
        _salt = _salt * PRIME_SALT + static_cast<uint32_t>(ptrHash);
        for(;;) {
            ObjHandle newHandle{_salt};
            // typically once, but will keep repeating if there is a handle
            // collision
            auto i = _handles.find(newHandle);
            if(i == _handles.end()) {
                // unused value found - note nested lock scope here
                // handle must be added to scope table before global table
                ObjectAnchor withHandle{partial.withHandle(newHandle)};
                scope->createRootHelper(withHandle);
                // Only do this unless createRootHelper succeeds - mitigates
                // resource leakage on error
                _handles.emplace(newHandle, withHandle);
                return withHandle;
            }
            // hash conflict, use 2nd algorithm to find an empty slot
            _salt += PRIME_INC;
        }
    }

    //
    // Removes typically come in via this path. It guarantees it's removed from
    // global handle table before removed from scope table
    //
    void HandleTable::remove(const ObjectAnchor &anchor) {
        ObjHandle h = anchor.getHandle();
        if(!h) {
            return;
        }
        std::shared_ptr<TrackingScope> scope{anchor.getOwner()};
        std::unique_lock guard{_mutex};
        // remove global first - mitigates resource leakage on error
        _handles.erase(h);
        guard.unlock();
        if(scope) {
            // scope can be unset if scope is in middle of being destroyed
            scope->removeRootHelper(anchor);
        }
    }
} // namespace data
