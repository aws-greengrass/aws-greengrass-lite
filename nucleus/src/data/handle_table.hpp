#pragma once
#include "safe_handle.hpp"
#include "tracked_object.hpp"
#include <cassert>
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
        std::weak_ptr<TrackingRoot> _root; // roots

    public:
        explicit WeakObjectAnchor(
            const std::weak_ptr<TrackedObject> &obj, const std::weak_ptr<TrackingRoot> &root)
            : _object{obj}, _root(root) {
        }

        explicit WeakObjectAnchor(const ObjectAnchor &anchor)
            : _object{anchor.getBase()}, _root(anchor.getRoot()) {
        }

        WeakObjectAnchor() = default;
        WeakObjectAnchor(const WeakObjectAnchor &) = default;
        WeakObjectAnchor(WeakObjectAnchor &&) noexcept = default;
        WeakObjectAnchor &operator=(const WeakObjectAnchor &) = default;
        WeakObjectAnchor &operator=(WeakObjectAnchor &&) = default;
        virtual ~WeakObjectAnchor() = default;

        explicit operator bool() const {
            return !(_object.expired() || _root.expired());
        }

        [[nodiscard]] ObjectAnchor lock() const {
            if(*this) {
                return ObjectAnchor{_object.lock(), _root.lock()};
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
        mutable scope::FixedPtr<HandleTable> _table{scope::FixedPtr<HandleTable>::of(this)};
        uint32_t _salt{0};
        std::unordered_map<
            ObjHandle::Partial,
            WeakObjectAnchor,
            ObjHandle::Partial::Hash,
            ObjHandle::Partial::CompEq>
            _handles;

        ObjHandle applyUnchecked(ObjHandle::Partial h) const {
            return {_table, h};
        }

    public:
        HandleTable() = default;
        HandleTable(const HandleTable &) = delete;
        HandleTable(HandleTable &&) noexcept = delete;
        ~HandleTable() = default;
        HandleTable &operator=(const HandleTable &) = delete;
        HandleTable &operator=(HandleTable &&) noexcept = delete;
        ObjHandle apply(ObjHandle::Partial h) const;
        // Retrieve object, safe if handle does not exist
        ObjectAnchor tryGet(ObjHandle::Partial handle) const;
        // Retrieve object, handle is expected to exist
        ObjectAnchor get(ObjHandle::Partial handle) const;

        // Creates a new handle, even if one exists, using
        // partially constructed anchor
        ObjectAnchor create(const ObjectAnchor &partFilled);
        // remove a handle
        void remove(const ObjectAnchor &anchor);

        bool isObjHandleValid(const ObjHandle::Partial handle) const {
            std::shared_lock guard{_mutex};
            return _handles.find(handle) != _handles.end();
        }

        void check(ObjHandle::Partial handle) const;

        ObjHandle::Partial partial(const ObjectAnchor &anchor) {
            if(anchor) {
                return partial(anchor.getHandle());
            } else {
                return {};
            }
        }

        ObjHandle::Partial partial(const ObjHandle &handle) {
            if(handle) {
                assert(this == &handle.table());
                return handle.partial();
            } else {
                return {};
            }
        }
    };
} // namespace data
