#pragma once
#include "safe_handle.h"
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

    class Environment;
    class HandleTable;
    class TrackedObject;
    class TrackingScope;

    //
    // Handles for objects only
    //
    typedef Handle<TrackedObject> ObjHandle;

    //
    // Base class for all objects that can be tracked with one or more handles
    // The object lives as long as there is one or more handles, or if there is
    // one or more std::shared_ptr<> reference to the object
    //
    class TrackedObject : public util::RefObject<TrackedObject> {
    protected:
        Environment &_environment;

    public:
        TrackedObject(const TrackedObject &) = delete;
        TrackedObject(TrackedObject &&) noexcept = default;
        TrackedObject &operator=(const TrackedObject &) = delete;
        TrackedObject &operator=(TrackedObject &&) noexcept = delete;
        virtual ~TrackedObject() = default;

        explicit TrackedObject(Environment &environment) : _environment{environment} {
        }
    };

    //
    // Copy-by-value class to track an association between a handle and a tracked
    // object ObjectAnchor is inherently not thread safe - but can be made thread
    // safe in containment
    //
    class ObjectAnchor {
    private:
        ObjHandle _handle{ObjHandle::nullHandle()};
        std::shared_ptr<TrackedObject> _object; // multiple anchors to one object
        std::weak_ptr<TrackingScope> _owner; // owner container

    public:
        explicit ObjectAnchor(
            const std::shared_ptr<TrackedObject> &obj, const std::weak_ptr<TrackingScope> &owner
        )
            : _object{obj}, _owner(owner) {
        }

        ObjectAnchor() = default;
        ObjectAnchor(const ObjectAnchor &) = default;
        ObjectAnchor(ObjectAnchor &&) noexcept = default;
        ObjectAnchor &operator=(const ObjectAnchor &) = default;
        ObjectAnchor &operator=(ObjectAnchor &&) = default;
        virtual ~ObjectAnchor() = default;

        explicit operator bool() const {
            return static_cast<bool>(_object);
        }

        template<typename T>
        [[nodiscard]] std::shared_ptr<T> getObject() const {
            if(*this) {
                return _object->ref<T>();
            } else {
                return {};
            }
        }

        [[nodiscard]] std::shared_ptr<TrackedObject> getBase() const {
            if(*this) {
                return _object->ref<TrackedObject>();
            } else {
                return {};
            }
        }

        [[nodiscard]] std::shared_ptr<TrackingScope> getOwner() const {
            if(*this) {
                return _owner.lock();
            } else {
                return {};
            }
        }

        [[nodiscard]] ObjHandle getHandle() const {
            return _handle;
        }

        [[nodiscard]] ObjectAnchor withNewScope(const std::shared_ptr<TrackingScope> &owner) const {
            return ObjectAnchor(_object, owner);
        }

        [[nodiscard]] ObjectAnchor withHandle(ObjHandle handle) const {
            ObjectAnchor copy{*this};
            copy._handle = handle;
            return copy;
        }

        void release();
    };

    //
    // A TrackedObject that represents a scope (Task, etc.)
    // ensuring all handles associated with that scope will be removed when that
    // scope exits.
    //
    class TrackingScope : public TrackedObject {
        friend class HandleTable;

    protected:
        std::map<ObjHandle, std::shared_ptr<TrackedObject>, ObjHandle::CompLess> _roots;
        mutable std::shared_mutex _mutex;
        void removeRootHelper(const ObjectAnchor &anchor);
        ObjectAnchor createRootHelper(const ObjectAnchor &anchor);
        std::vector<ObjectAnchor> getRootsHelper(const std::weak_ptr<TrackingScope> &assumedOwner);

    public:
        explicit TrackingScope(Environment &environment) : TrackedObject{environment} {
        }

        TrackingScope(const TrackingScope &) = delete;
        TrackingScope(TrackingScope &&) noexcept = delete;
        TrackingScope &operator=(const TrackingScope &) = delete;
        TrackingScope &operator=(TrackingScope &&) noexcept = delete;
        ~TrackingScope() override;
        ObjectAnchor anchor(const std::shared_ptr<TrackedObject> &obj);
        ObjectAnchor reanchor(const ObjectAnchor &anchor);
        void remove(const ObjectAnchor &anchor);

        std::shared_ptr<const TrackingScope> scopeRef() const {
            return ref<TrackingScope>();
        }

        std::shared_ptr<TrackingScope> scopeRef() {
            return ref<TrackingScope>();
        }

        std::vector<ObjectAnchor> getRoots() {
            return getRootsHelper(scopeRef());
        }
    };

} // namespace data
