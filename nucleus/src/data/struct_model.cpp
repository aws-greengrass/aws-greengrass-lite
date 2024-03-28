#include "struct_model.hpp"
#include "conv/json_conv.hpp"
#include "conv/yaml_conv.hpp"
#include "scope/context_full.hpp"

namespace data {
    // NOLINTNEXTLINE(*-no-recursion)
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

    std::shared_ptr<data::SharedBuffer> ContainerModelBase::toYaml() {
        return conv::YamlHelper::serializeToBuffer(context(), baseRef());
    }

    void StructModelBase::put(std::string_view sv, const StructElement &element) {
        Symbol handle = context()->symbols().intern(sv);
        put(handle, element);
    }

    void StructModelBase::put(data::Symbol handle, const data::StructElement &element) {
        if(element.isBoxed()) {
            putImpl(handle, element.unbox());
        } else {
            putImpl(handle, element);
        }
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
                // Auto-box object may delay a real error, but also allows more options for
                // doing the right thing.
                return getBoxed();
        }
    }

    void StructElement::visit(Archive &archive) {
        archive(_value);
    }

    StructElement StructElement::unbox() const {
        if(_value.index() == OBJECT) {
            auto ptr = std::get<std::shared_ptr<TrackedObject>>(_value);
            auto boxed = std::dynamic_pointer_cast<Boxed>(ptr);
            if(boxed) {
                return boxed->get();
            }
        }
        return *this;
    }

    StructElement StructElement::autoUnbox(std::string_view desiredTypeForError) const {
        if(_value.index() == OBJECT) {
            auto ptr = std::get<std::shared_ptr<TrackedObject>>(_value);
            auto boxed = std::dynamic_pointer_cast<Boxed>(ptr);
            if(boxed) {
                return boxed->get();
            }
        }
        throw(std::runtime_error{
            std::string("Unsupported type conversion to ") + desiredTypeForError});
    }

    void Boxed::visit(Archive &archive) {
        std::unique_lock guard{_mutex};
        archive(_value);
    }

    std::shared_ptr<ContainerModelBase> Boxed::clone() const {
        auto clone = std::make_shared<Boxed>(context());
        clone->put(get());
        return clone;
    }

    std::shared_ptr<ContainerModelBase> StructModelBase::clone() const {
        return copy(); // Delegate
    }

    std::shared_ptr<ContainerModelBase> ListModelBase::clone() const {
        return copy(); // Delegate
    }

    std::shared_ptr<ArchiveAdapter> StructArchiver::key(const Symbol &symbol) {
        return std::make_shared<StructKeyArchiver>(_model, _model->foldKey(symbol, isIgnoreCase()));
    }

    void StructArchiver::visit(ValueType &vt) {
        throw std::runtime_error("Unsupported visit");
    }

    std::vector<Symbol> StructArchiver::keys() const {
        return _model->getKeys();
    }

    std::shared_ptr<ArchiveAdapter> StructKeyArchiver::key(const Symbol &symbol) {
        auto e = _model->get(_key);
        std::shared_ptr<StructModelBase> refStruct;
        if(e.isNull()) {
            refStruct = _model->createForChild();
            _model->put(_key, refStruct);
        } else if(e.isStruct()) {
            refStruct = e.castObject<StructModelBase>();
        }
        if(refStruct) {
            return std::make_shared<StructKeyArchiver>(
                refStruct, refStruct->foldKey(symbol, isIgnoreCase()));
        } else {
            throw std::runtime_error("Key type mismatch");
        }
    }

    std::vector<Symbol> StructKeyArchiver::keys() const {
        auto e = _model->get(_key);
        return ArchiveTraits::toKeys(e);
    }

    std::shared_ptr<ArchiveAdapter> StructKeyArchiver::list() {
        auto e = _model->get(_key);
        std::shared_ptr<ListModelBase> refList;
        if(e.isNull()) {
            refList = std::make_shared<SharedList>(scope::context());
            _model->put(_key, refList);
        } else if(e.isList()) {
            refList = e.castObject<ListModelBase>();
        }
        if(refList) {
            return std::make_shared<ListArchiver>(refList);
        } else {
            throw std::runtime_error("List type mismatch");
        }
    }

    bool StructKeyArchiver::isList() const noexcept {
        auto e = _model->get(_key);
        return e.isList();
    }

    bool StructKeyArchiver::hasValue() const {
        auto e = _model->get(_key);
        return !e.isNull();
    }

    void StructKeyArchiver::visit(ValueType &vt) {
        _model->put(_key, vt);
    }

    void StructModelBase::visit(Archive &archive) {
        if(archive.isArchiving()) {
            // Dearchive self into archive
            Archive self = Archive(std::make_shared<ElementDearchiver>(ref<StructModelBase>()));
            archive(self);
        } else {
            // Dearchive into self which is put into archive mode
            Archive self = Archive(std::make_shared<StructArchiver>(ref<StructModelBase>()));
            archive(self);
        }
    }

    void ListModelBase::visit(Archive &archive) {
        if(archive.isArchiving()) {
            Archive self = Archive(std::make_shared<ListDearchiver>(ref<ListModelBase>()));
            auto target = archive->list();
            while(self->canVisit()) {
                ValueType v;
                self->visit(v);
                target->visit(v);
                self->advance();
                target->advance();
            }
        } else {
            Archive self = Archive(std::make_shared<ListArchiver>(ref<ListModelBase>()));
            auto source = archive->list();
            while(source->canVisit()) {
                ValueType v;
                source->visit(v);
                self->visit(v);
                source->advance();
                self->advance();
            }
        }
    }

    void ListArchiver::visit(data::ValueType &vt) {
        _list->put(_index, vt);
    }

    std::shared_ptr<ArchiveAdapter> ListArchiver::list() {
        auto entry = std::make_shared<SharedList>(scope::context());
        _list->put(_index, entry);
        return std::make_shared<ListArchiver>(entry);
    }

    bool ListArchiver::advance() noexcept {
        ++_index;
        return true;
    }

    bool ListDearchiver::canVisit() const {
        return _index < _size;
    }

    bool ListDearchiver::advance() noexcept {
        if(canVisit()) {
            ++_index;
            return canVisit();
        }
        return false;
    }

    StructElement ListDearchiver::read() const {
        if(_index < _size) {
            return _list->get(_index);
        } else {
            return {};
        }
    }

} // namespace data
