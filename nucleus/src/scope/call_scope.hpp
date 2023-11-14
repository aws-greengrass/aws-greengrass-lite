#pragma once
#include "data/tracked_object.hpp"
#include "scope/context.hpp"
#include <memory>

namespace scope {

    // A scope that is intended to be stack based, is split into two, and should be used
    // via the CallScope stack class.
    class CallScope : public data::TrackingScope {
        data::ObjHandle _self;
        mutable std::shared_mutex _mutex;

        void setSelf(const data::ObjHandle &handle) {
            _self = handle;
        }

    public:
        explicit CallScope(const std::shared_ptr<Context> &context) : data::TrackingScope(context) {
        }

        [[nodiscard]] static std::shared_ptr<CallScope> create(
            const std::shared_ptr<Context> &context,
            const std::shared_ptr<data::TrackingRoot> &root);
        void release();

        data::ObjHandle getSelf() {
            return _self;
        }
    };

} // namespace scope
