#include "struct_model.hpp"
#include "conv/json_conv.hpp"
#include "scope/context_full.hpp"

namespace data {
    void ContainerModelBase::checkedPut(
        const StructElement &element, const std::function<void(const StructElement &)> &putAction) {
        auto ctx = context();
        std::unique_lock cycleGuard{ctx->cycleCheckMutex(), std::defer_lock};

        if(element.isContainer()) {
            std::shared_ptr<data::ContainerModelBase> otherContainer = element.getContainer();
            if(otherContainer) {
                // prepare for cycle checks
                // cycle checking requires obtaining the cycle check mutex
                // the structure mutex must be acquired after cycle check mutex
                // TODO: there has to be a better way
                cycleGuard.lock();
                otherContainer->rootsCheck(this);
            }
        } else if(element.isType<TrackingScope>()) {
            std::shared_ptr<data::TrackingScope> complexScope =
                element.castObject<data::TrackingScope>();
            // TODO: Need to catch cycles with TrackingScope objects
        }

        // cycleGuard may still be locked - intentional
        putAction(element);
    }

    std::shared_ptr<data::SharedBuffer> ContainerModelBase::toJson() {
        return conv::JsonHelper::serializeToBuffer(context(), baseRef());
    }

    void StructModelBase::put(std::string_view sv, const StructElement &element) {
        Symbol handle = context()->symbols().intern(sv);
        putImpl(handle, element);
    }

    void StructModelBase::put(data::Symbol handle, const data::StructElement &element) {
        putImpl(handle, element);
    }

    bool StructModelBase::hasKey(const std::string_view sv) const {
        Symbol handle = context()->symbols().intern(sv);
        return hasKeyImpl(handle);
    }

    bool StructModelBase::hasKey(data::Symbol handle) const {
        return hasKeyImpl(handle);
    }

    StructElement StructModelBase::get(std::string_view sv) const {
        Symbol handle = context()->symbols().intern(sv);
        return getImpl(handle);
    }

    StructElement StructModelBase::get(data::Symbol handle) const {
        return getImpl(handle);
    }

    void Boxed::rootsCheck(const ContainerModelBase *target) const { // NOLINT(*-no-recursion)
        if(this == target) {
            throw std::runtime_error("Recursive reference of container");
        }
        // we don't want to keep nesting locks else we will deadlock, retrieve under lock
        // and use outside of lock.
        std::shared_lock guard{_mutex};
        std::shared_ptr<ContainerModelBase> other;
        if(_value.isContainer()) {
            other = _value.getContainer();
        }
        guard.unlock();
        if(other) {
            other->rootsCheck(target);
        }
    }

    void Boxed::put(const StructElement &element) {
        checkedPut(element, [this](auto &el) {
            std::unique_lock guard{_mutex};
            _value = el;
        });
    }

    StructElement Boxed::get() const {
        std::shared_lock guard{_mutex};
        return _value;
    }

    std::shared_ptr<data::ContainerModelBase> Boxed::box(
        const scope::UsingContext &context, const StructElement &element) {
        if(element.isContainer() || element.isNull()) {
            return element.getContainer();
        }
        auto boxed = std::make_shared<Boxed>(context);
        boxed->put(element);
        return boxed;
    }

    uint32_t Boxed::size() const {
        std::shared_lock guard{_mutex};
        return _value.isNull() ? 0 : 1;
    }

    std::shared_ptr<ContainerModelBase> StructElement::getBoxed() const {
        return Boxed::box(scope::context(), *this);
    }

    std::shared_ptr<TrackedObject> StructElement::getObject() const {
        switch(_value.index()) {
            case NONE:
                return {};
            case OBJECT:
                return std::get<std::shared_ptr<TrackedObject>>(_value);
            default:
                return getBoxed();
        }
    }
} // namespace data
