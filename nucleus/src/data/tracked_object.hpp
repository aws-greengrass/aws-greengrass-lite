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
#include <ref_object.hpp>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace data {

    class HandleTable;
    class TrackedObject;
    class TrackingScope;

    /**
     * Handles for object roots. Note that RootHandles are singletons (like std::unique_ptr). When
     * the handle object is destroyed, all linked object handles are destroyed with it.
     */
    class RootHandle : public Handle<HandleTable> {

    private:
        bool release() noexcept;

    public:
        RootHandle() noexcept = default;
        RootHandle(const RootHandle &) noexcept = delete;
        RootHandle(RootHandle &&) noexcept = default;
        RootHandle &operator=(const RootHandle &) noexcept = delete;
        RootHandle &operator=(RootHandle &&) noexcept = default;
        ~RootHandle() noexcept {
            release();
        }

        constexpr RootHandle(scope::FixedPtr<HandleTable> table, Partial h) noexcept
            : Handle(table, h) {
        }
    };

    /**
     * Handles for objects.
     */
    class ObjHandle : public Handle<HandleTable> {
    private:
        // Reduces code duplication of toObject() and simplifies header
        std::shared_ptr<TrackedObject> toObjectHelper() const;

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

        template<typename T = data::TrackedObject>
        std::shared_ptr<T> toObject() const;

        bool release() noexcept;
    };

    /**
     * Base class for all objects that can be tracked with object handles. A TrackedObject must
     * always be used as std::shared_ptr. The object lives as long as there is at least one
     * reference, where each object handle contributes a reference in addition to Nucleus
     * references.
     */
    class TrackedObject : public util::RefObject<TrackedObject>, protected scope::UsesContext {

    public:
        // Used by toObject() template to throw the correct exception
        using BadCastError = std::bad_cast;

        TrackedObject(TrackedObject &&) noexcept = default;
        virtual ~TrackedObject() noexcept = default;
        TrackedObject(const TrackedObject &) = delete;
        TrackedObject &operator=(const TrackedObject &) = delete;
        TrackedObject &operator=(TrackedObject &&) noexcept = delete;

        using scope::UsesContext::UsesContext;

        virtual void close() {
            // meaning of close depends on object - by default it's a no-op
            // TODO: Should it be a no-op or exception?
        }
    };

    /**
     * Checked cast from handle to object of given type.
     * @tparam T New Type of object
     * @return Object with given type
     */
    template<typename T>
    std::shared_ptr<T> ObjHandle::toObject() const {
        static_assert(std::is_base_of_v<TrackedObject, T>);
        if(isNull()) {
            return {};
        }
        std::shared_ptr<T> ref = std::dynamic_pointer_cast<T>(toObjectHelper());
        if(!ref) {
            throw typename T::BadCastError();
        }
        return ref;
    }

    /**
     * Tracking scope is the base class for handles that manage scope - namely modules
     */
    class TrackingScope : public TrackedObject {
    protected:
        RootHandle _root;

    public:
        using BadCastError = errors::InvalidScopeError;

        explicit TrackingScope(const scope::UsingContext &context);

        RootHandle &root() {
            return _root;
        }
    };

} // namespace data
