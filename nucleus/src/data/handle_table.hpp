#pragma once
#include "tracked_object.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <util.hpp>

namespace data {

    //
    // Track objects and owners with no reference counting. The scope owner
    // maintains the reference count
    //
    class WeakObjectAnchor {
    private:
        std::weak_ptr<TrackedObject> _object; // multiple anchors to one object
        std::weak_ptr<TrackingScope> _owner; // owner container

    public:
        explicit WeakObjectAnchor(
            const std::weak_ptr<TrackedObject> &obj, const std::weak_ptr<TrackingScope> &owner
        )
            : _object{obj}, _owner(owner) {
        }

        explicit WeakObjectAnchor(const ObjectAnchor &anchor)
            : _object{anchor.getBase()}, _owner(anchor.getOwner()) {
        }

        WeakObjectAnchor() = default;
        WeakObjectAnchor(const WeakObjectAnchor &) = default;
        WeakObjectAnchor(WeakObjectAnchor &&) noexcept = default;
        WeakObjectAnchor &operator=(const WeakObjectAnchor &) = default;
        WeakObjectAnchor &operator=(WeakObjectAnchor &&) = default;
        virtual ~WeakObjectAnchor() = default;

        explicit operator bool() const {
            return !(_object.expired() || _owner.expired());
        }

        [[nodiscard]] ObjectAnchor lock() const {
            if(*this) {
                return ObjectAnchor{_object.lock(), _owner.lock()};
            } else {
                return {};
            }
        }
    };

    //
    // Handle tracking
    //
    class HandleTable {
        static const int PRIME_SALT{7};
        static const int PRIME1{11};
        static const int PRIME2{431};
        static const int PRIME_INC{15299};
        mutable std::shared_mutex _mutex;
        uint32_t _salt{0};
        std::unordered_map<ObjHandle, WeakObjectAnchor, ObjHandle::Hash, ObjHandle::CompEq>
            _handles;

    public:
        // Retrieve object, safe if handle does not exist
        static ObjectAnchor tryGet(ObjHandle handle) ;
        // Retrieve object, handle is expected to exist
        ObjectAnchor get(ObjHandle handle) const;

        // Convenience function
        template<typename T>
        std::shared_ptr<T> getObject(ObjHandle handle) const {
            return get(handle).getObject<T>();
        }

        // Creates a new handle, even if one exists
        ObjectAnchor create(const ObjectAnchor &partial);
        // remove a handle
        static void remove(const ObjectAnchor &anchor);
    };
} // namespace data
