#pragma once
#include "data/safe_handle.hpp"
#include "errors/errors.hpp"
#include "scope/context.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <util.hpp>
#include <vector>

namespace data {

    class HandleTable;
    class TrackedObject;
    class TrackingRoot;
    class TrackingScope;

    //
    // Handles for objects only
    //
    class ObjHandle : public Handle<HandleTable> {
    public:
        constexpr ObjHandle() noexcept = default;
        constexpr ObjHandle(const ObjHandle &) noexcept = default;
        constexpr ObjHandle(ObjHandle &&) noexcept = default;
        constexpr ObjHandle &operator=(const ObjHandle &) noexcept = default;
        constexpr ObjHandle &operator=(ObjHandle &&) noexcept = default;
        ~ObjHandle() noexcept = default;

        constexpr ObjHandle(scope::FixedPtr<HandleTable> table, Partial h) noexcept
            : Handle(table, h) {
        }

        ObjectAnchor toAnchor() const;

        template<typename T = data::TrackedObject>
        std::shared_ptr<T> toObject() const;
    };

    //
    // Base class for all objects that can be tracked with one or more handles
    // The object lives as long as there is one or more handles, or if there is
    // one or more std::shared_ptr<> reference to the object
    //
    class TrackedObject : public util::RefObject<TrackedObject>, protected scope::UsesContext {

    public:
        using BadCastError = std::bad_cast;

        TrackedObject(const TrackedObject &) = delete;
        TrackedObject(TrackedObject &&) noexcept = default;
        TrackedObject &operator=(const TrackedObject &) = delete;
        TrackedObject &operator=(TrackedObject &&) noexcept = delete;
        virtual ~TrackedObject() = default;

        explicit TrackedObject(const scope::UsingContext &context) : UsesContext(context) {
        }

        virtual void beforeRemove(const ObjectAnchor &anchor) {
            // Allow special cleanup when specified handle is removed
        }
    };

    //
    // Copy-by-value class to track an association between a handle and a tracked
    // object ObjectAnchor is inherently not thread safe - but can be made thread
    // safe in containment
    //
    class ObjectAnchor {
    private:
        ObjHandle _handle{};
        std::shared_ptr<TrackedObject> _object; // multiple anchors to one object
        std::weak_ptr<TrackingRoot> _root; // root object owns handle scope

    public:
        explicit ObjectAnchor(
            const std::shared_ptr<TrackedObject> &obj, const std::weak_ptr<TrackingRoot> &root)
            : _object{obj}, _root(root) {
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

        template<typename T = data::TrackedObject>
        [[nodiscard]] std::shared_ptr<T> getObject() const {
            if(*this) {
                try {
                    return _object->ref<T>();
                } catch(std::bad_cast &) {
                    throw typename T::BadCastError();
                }
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

        [[nodiscard]] std::shared_ptr<TrackingRoot> getRoot() const {
            if(*this) {
                return _root.lock();
            } else {
                return {};
            }
        }

        [[nodiscard]] ObjHandle getHandle() const {
            return _handle;
        }

        [[nodiscard]] ObjectAnchor withNewRoot(const std::shared_ptr<TrackingRoot> &root) const {
            return ObjectAnchor(_object, root);
        }

        [[nodiscard]] ObjectAnchor withHandle(ObjHandle handle) const {
            ObjectAnchor copy{*this};
            copy._handle = handle;
            return copy;
        }

        [[nodiscard]] uint32_t asIntHandle() const {
            return getHandle().asInt();
        }

        void release();
    };

    template<typename T>
    std::shared_ptr<T> ObjHandle::toObject() const {
        if(*this) {
            return toAnchor().getObject<T>();
        } else {
            return {};
        }
    }

    //
    // Class for managing object roots. A container for all anchors.
    //
    class TrackingRoot : public util::RefObject<TrackingRoot>, protected scope::UsesContext {
        friend class HandleTable;

    protected:
        std::map<ObjHandle::Partial, std::shared_ptr<TrackedObject>, ObjHandle::Partial::CompLess>
            _roots;
        mutable std::shared_mutex _mutex;
        void removeRootHelper(const ObjectAnchor &anchor);
        ObjectAnchor createRootHelper(const ObjectAnchor &anchor);
        std::vector<ObjectAnchor> getRootsHelper(const std::weak_ptr<TrackingRoot> &assumedOwner);

    public:
        explicit TrackingRoot(const scope::UsingContext &context) : scope::UsesContext(context) {
        }

        TrackingRoot(const TrackingRoot &) = delete;
        TrackingRoot(TrackingRoot &&) noexcept = delete;
        TrackingRoot &operator=(const TrackingRoot &) = delete;
        TrackingRoot &operator=(TrackingRoot &&) noexcept = delete;
        ~TrackingRoot();
        ObjectAnchor anchor(const std::shared_ptr<TrackedObject> &obj);
        void remove(const ObjectAnchor &anchor);

        std::vector<ObjectAnchor> getRoots() {
            return getRootsHelper(baseRef());
        }
    };

    //
    // Tracking scope is the base class for handles that manage scope - namely modules
    // and call scope
    //
    class TrackingScope : public TrackedObject {
    protected:
        std::shared_ptr<TrackingRoot> _root;

    public:
        using BadCastError = errors::InvalidScopeError;

        explicit TrackingScope(const scope::UsingContext &context);

        TrackingScope(const TrackingScope &) = delete;
        TrackingScope(TrackingScope &&) noexcept = delete;
        TrackingScope &operator=(const TrackingScope &) = delete;
        TrackingScope &operator=(TrackingScope &&) noexcept = delete;
        ~TrackingScope() override;

        std::shared_ptr<TrackingRoot> root() const {
            return _root;
        }
    };

} // namespace data
