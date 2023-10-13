#include "data/globals.hpp"
#include "data/shared_buffer.hpp"
#include "data/shared_list.hpp"
#include "data/shared_struct.hpp"
#include <cpp_api.hpp>
#include <util.hpp>

using namespace data;

uint32_t ggapiGetStringOrdinal(const char *bytes, size_t len) noexcept {
    try {
        Global &global = Global::self();
        return global.environment.stringTable.getOrCreateOrd(std::string{bytes, len}).asInt();
    } catch(...) {
        std::terminate(); // any string table put errors would be a critical
                          // error requiring termination
    }
}

size_t ggapiGetOrdinalString(uint32_t ord, char *bytes, size_t len) noexcept {
    return ggapi::trapErrorReturn<size_t>([ord, bytes, len]() {
        Global &global = Global::self();
        StringOrd ordH = StringOrd{ord};
        global.environment.stringTable.assertStringHandle(ordH);
        std::string s{global.environment.stringTable.getString(ordH)};
        if(s.length() > len) {
            throw std::runtime_error("Destination buffer is too small");
        }
        util::Span span(bytes, len);
        return span.copyFrom(s.begin(), s.end());
    });
}

size_t ggapiGetOrdinalStringLen(uint32_t ord) noexcept {
    return ggapi::trapErrorReturn<size_t>([ord]() {
        Global &global = Global::self();
        StringOrd ordH = StringOrd{ord};
        global.environment.stringTable.assertStringHandle(ordH);
        std::string s{global.environment.stringTable.getString(ordH)};
        return s.length();
    });
}

uint32_t ggapiCreateStruct(uint32_t anchorHandle) noexcept {
    if(anchorHandle == 0) {
        anchorHandle = tasks::Task::getThreadSelf().asInt();
    }
    return ggapi::trapErrorReturn<uint32_t>([anchorHandle]() {
        Global &global = Global::self();
        auto ss{std::make_shared<SharedStruct>(global.environment)};
        auto owner{
            global.environment.handleTable.getObject<TrackingScope>(ObjHandle{anchorHandle})};
        return owner->anchor(ss).getHandle().asInt();
    });
}

uint32_t ggapiCreateList(uint32_t anchorHandle) noexcept {
    if(anchorHandle == 0) {
        anchorHandle = tasks::Task::getThreadSelf().asInt();
    }
    return ggapi::trapErrorReturn<uint32_t>([anchorHandle]() {
        Global &global = Global::self();
        auto ss{std::make_shared<SharedList>(global.environment)};
        auto owner{
            global.environment.handleTable.getObject<TrackingScope>(ObjHandle{anchorHandle})};
        return owner->anchor(ss).getHandle().asInt();
    });
}

uint32_t ggapiCreateBuffer(uint32_t anchorHandle) noexcept {
    if(anchorHandle == 0) {
        anchorHandle = tasks::Task::getThreadSelf().asInt();
    }
    return ggapi::trapErrorReturn<uint32_t>([anchorHandle]() {
        Global &global = Global::self();
        auto ss{std::make_shared<SharedBuffer>(global.environment)};
        auto owner{
            global.environment.handleTable.getObject<TrackingScope>(ObjHandle{anchorHandle})};
        return owner->anchor(ss).getHandle().asInt();
    });
}

bool ggapiStructPutBool(uint32_t structHandle, uint32_t ord, bool value) noexcept {
    return ggapi::trapErrorReturn<bool>([structHandle, ord, value]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<StructModelBase>(ObjHandle{structHandle})};
        StringOrd ordH = StringOrd{ord};
        StructElement newElement{value};
        ss->put(ordH, newElement);
        return true;
    });
}

bool ggapiListPutBool(uint32_t listHandle, int32_t idx, bool value) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, value]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<ListModelBase>(ObjHandle{listHandle})};
        StructElement newElement{value};
        ss->put(idx, newElement);
        return true;
    });
}

bool ggapiListInsertBool(uint32_t listHandle, int32_t idx, bool value) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, value]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<ListModelBase>(ObjHandle{listHandle})};
        StructElement newElement{value};
        ss->insert(idx, newElement);
        return true;
    });
}

bool ggapiStructPutInt64(uint32_t structHandle, uint32_t ord, uint64_t value) noexcept {
    return ggapi::trapErrorReturn<bool>([structHandle, ord, value]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<StructModelBase>(ObjHandle{structHandle})};
        StringOrd ordH = StringOrd{ord};
        StructElement newElement{value};
        ss->put(ordH, newElement);
        return true;
    });
}

bool ggapiListPutInt64(uint32_t listHandle, int32_t idx, uint64_t value) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, value]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<ListModelBase>(ObjHandle{listHandle})};
        StructElement newElement{value};
        ss->put(idx, newElement);
        return true;
    });
}

bool ggapiListInsertInt64(uint32_t listHandle, int32_t idx, uint64_t value) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, value]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<ListModelBase>(ObjHandle{listHandle})};
        StructElement newElement{value};
        ss->insert(idx, newElement);
        return true;
    });
}

bool ggapiStructPutFloat64(uint32_t structHandle, uint32_t ord, double value) noexcept {
    return ggapi::trapErrorReturn<bool>([structHandle, ord, value]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<StructModelBase>(ObjHandle{structHandle})};
        StringOrd ordH = StringOrd{ord};
        StructElement newElement{value};
        ss->put(ordH, newElement);
        return true;
    });
}

bool ggapiListPutFloat64(uint32_t listHandle, int32_t idx, double value) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, value]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<ListModelBase>(ObjHandle{listHandle})};
        StructElement newElement{value};
        ss->put(idx, newElement);
        return true;
    });
}

bool ggapiListInsertFloat64(uint32_t listHandle, int32_t idx, double value) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, value]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<ListModelBase>(ObjHandle{listHandle})};
        StructElement newElement{value};
        ss->insert(idx, newElement);
        return true;
    });
}

bool ggapiStructPutString(
    uint32_t structHandle, uint32_t ord, const char *bytes, size_t len
) noexcept {
    return ggapi::trapErrorReturn<bool>([structHandle, ord, bytes, len]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<StructModelBase>(ObjHandle{structHandle})};
        StringOrd ordH = StringOrd{ord};
        StructElement newElement{std::string(bytes, len)};
        ss->put(ordH, newElement);
        return true;
    });
}

bool ggapiListPutString(uint32_t listHandle, int32_t idx, const char *bytes, size_t len) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, bytes, len]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<ListModelBase>(ObjHandle{listHandle})};
        StructElement newElement{std::string(bytes, len)};
        ss->put(idx, newElement);
        return true;
    });
}

bool ggapiListInsertString(
    uint32_t listHandle, int32_t idx, const char *bytes, size_t len
) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, bytes, len]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<ListModelBase>(ObjHandle{listHandle})};
        StructElement newElement{std::string(bytes, len)};
        ss->insert(idx, newElement);
        return true;
    });
}

bool ggapiStructPutHandle(uint32_t structHandle, uint32_t ord, uint32_t nestedHandle) noexcept {
    return ggapi::trapErrorReturn<bool>([structHandle, ord, nestedHandle]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<StructModelBase>(ObjHandle{structHandle})};
        auto s2{
            global.environment.handleTable.getObject<ContainerModelBase>(ObjHandle{nestedHandle})};
        StringOrd ordH = StringOrd{ord};
        StructElement newElement{s2};
        ss->put(ordH, newElement);
        return true;
    });
}

bool ggapiListPutHandle(uint32_t listHandle, int32_t idx, uint32_t nestedHandle) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, nestedHandle]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<ListModelBase>(ObjHandle{listHandle})};
        auto s2{
            global.environment.handleTable.getObject<ContainerModelBase>(ObjHandle{nestedHandle})};
        StructElement newElement{s2};
        ss->put(idx, newElement);
        return true;
    });
}

bool ggapiListInsertHandle(uint32_t listHandle, int32_t idx, uint32_t nestedHandle) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, nestedHandle]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<ListModelBase>(ObjHandle{listHandle})};
        auto s2{
            global.environment.handleTable.getObject<ContainerModelBase>(ObjHandle{nestedHandle})};
        StructElement newElement{s2};
        ss->insert(idx, newElement);
        return true;
    });
}

bool ggapiBufferPut(uint32_t listHandle, int32_t idx, const char *bytes, uint32_t len) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, bytes, len]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<SharedBuffer>(ObjHandle{listHandle})};
        ConstMemoryView buffer{bytes, len};
        ss->put(idx, buffer);
        return true;
    });
}

bool ggapiBufferInsert(uint32_t listHandle, int32_t idx, const char *bytes, uint32_t len) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, bytes, len]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<SharedBuffer>(ObjHandle{listHandle})};
        ConstMemoryView buffer{bytes, len};
        ss->insert(idx, buffer);
        return true;
    });
}

bool ggapiStructHasKey(uint32_t structHandle, uint32_t ord) noexcept {
    return ggapi::trapErrorReturn<bool>([structHandle, ord]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<StructModelBase>(ObjHandle{structHandle})};
        StringOrd ordH = StringOrd{ord};
        return ss->hasKey(ordH);
    });
}

uint32_t ggapiGetSize(uint32_t listHandle) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([listHandle]() {
        Global &global = Global::self();
        auto ss{
            global.environment.handleTable.getObject<ContainerModelBase>(ObjHandle{listHandle})};
        return ss->size();
    });
}

bool ggapiStructGetBool(uint32_t structHandle, uint32_t ord) noexcept {
    return ggapi::trapErrorReturn<bool>([structHandle, ord]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<StructModelBase>(ObjHandle{structHandle})};
        StringOrd ordH = StringOrd{ord};
        return ss->get(ordH).getBool();
    });
}

bool ggapiListGetBool(uint32_t listHandle, int32_t idx) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<ListModelBase>(ObjHandle{listHandle})};
        return ss->get(idx).getBool();
    });
}

uint64_t ggapiStructGetInt64(uint32_t structHandle, uint32_t ord) noexcept {
    return ggapi::trapErrorReturn<uint64_t>([structHandle, ord]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<StructModelBase>(ObjHandle{structHandle})};
        StringOrd ordH = StringOrd{ord};
        return static_cast<uint64_t>(ss->get(ordH));
    });
}

uint64_t ggapiListGetInt64(uint32_t listHandle, int32_t idx) noexcept {
    return ggapi::trapErrorReturn<uint64_t>([listHandle, idx]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<ListModelBase>(ObjHandle{listHandle})};
        return static_cast<uint64_t>(ss->get(idx));
    });
}

double ggapiStructGetFloat64(uint32_t structHandle, uint32_t ord) noexcept {
    return ggapi::trapErrorReturn<double>([structHandle, ord]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<StructModelBase>(ObjHandle{structHandle})};
        StringOrd ordH = StringOrd{ord};
        return static_cast<double>(ss->get(ordH));
    });
}

double ggapiListGetFloat64(uint32_t listHandle, int32_t idx) noexcept {
    return ggapi::trapErrorReturn<double>([listHandle, idx]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<ListModelBase>(ObjHandle{listHandle})};
        return static_cast<double>(ss->get(idx));
    });
}

uint32_t ggapiStructGetHandle(uint32_t structHandle, uint32_t ord) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([structHandle, ord]() {
        Global &global = Global::self();
        ObjectAnchor ss_anchor{global.environment.handleTable.get(ObjHandle{structHandle})};
        std::shared_ptr<TrackingScope> ss_root{ss_anchor.getOwner()};
        auto ss{ss_anchor.getObject<StructModelBase>()};
        StringOrd ordH = StringOrd{ord};
        std::shared_ptr<ContainerModelBase> v = ss->get(ordH).getContainer();
        return ss_root->anchor(v).getHandle().asInt();
    });
}

uint32_t ggapiListGetHandle(uint32_t listHandle, int32_t idx) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([listHandle, idx]() {
        Global &global = Global::self();
        ObjectAnchor ss_anchor{global.environment.handleTable.get(ObjHandle{listHandle})};
        std::shared_ptr<TrackingScope> ss_root{ss_anchor.getOwner()};
        auto ss{ss_anchor.getObject<ListModelBase>()};
        std::shared_ptr<ContainerModelBase> v = ss->get(idx).getContainer();
        return ss_root->anchor(v).getHandle().asInt();
    });
}

size_t ggapiStructGetStringLen(uint32_t structHandle, uint32_t ord) noexcept {
    return ggapi::trapErrorReturn<size_t>([structHandle, ord]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<StructModelBase>(ObjHandle{structHandle})};
        StringOrd ordH = StringOrd{ord};
        std::string s = ss->get(ordH).getString();
        return s.length();
    });
}

size_t ggapiStructGetString(
    uint32_t structHandle, uint32_t ord, char *buffer, size_t buflen
) noexcept {
    return ggapi::trapErrorReturn<size_t>([structHandle, ord, buffer, buflen]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<StructModelBase>(ObjHandle{structHandle})};
        StringOrd ordH = StringOrd{ord};
        std::string s = ss->get(ordH).getString();
        if(s.length() > buflen) {
            throw std::runtime_error("Destination buffer is too small");
        }
        util::Span span(buffer, buflen);
        return span.copyFrom(s.begin(), s.end());
    });
}

size_t ggapiListGetStringLen(uint32_t listHandle, int32_t idx) noexcept {
    return ggapi::trapErrorReturn<size_t>([listHandle, idx]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<ListModelBase>(ObjHandle{listHandle})};
        std::string s = ss->get(idx).getString();
        return s.length();
    });
}

size_t ggapiListGetString(uint32_t listHandle, int32_t idx, char *buffer, size_t buflen) noexcept {
    return ggapi::trapErrorReturn<size_t>([listHandle, idx, buffer, buflen]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<ListModelBase>(ObjHandle{listHandle})};
        std::string s = ss->get(idx).getString();
        if(s.length() > buflen) {
            throw std::runtime_error("Destination buffer is too small");
        }
        util::Span span(buffer, buflen);
        return span.copyFrom(s.begin(), s.end());
    });
}

uint32_t ggapiBufferGet(uint32_t listHandle, int32_t idx, char *bytes, uint32_t len) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([listHandle, idx, bytes, len]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<SharedBuffer>(ObjHandle{listHandle})};
        MemoryView buffer{bytes, len};
        return ss->get(idx, buffer);
    });
}

uint32_t ggapiAnchorHandle(uint32_t anchorHandle, uint32_t objectHandle) noexcept {
    if(anchorHandle == 0) {
        anchorHandle = tasks::Task::getThreadSelf().asInt();
    }
    return ggapi::trapErrorReturn<uint32_t>([anchorHandle, objectHandle]() {
        Global &global = Global::self();
        auto ss{global.environment.handleTable.getObject<TrackedObject>(ObjHandle{objectHandle})};
        auto owner{
            global.environment.handleTable.getObject<TrackingScope>(ObjHandle{anchorHandle})};
        return owner->anchor(ss).getHandle().asInt();
    });
}

bool ggapiReleaseHandle(uint32_t objectHandle) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([objectHandle]() {
        Global &global = Global::self();
        ObjectAnchor anchored{global.environment.handleTable.get(ObjHandle{objectHandle})};
        // releasing a non-existing handle is a no-op as it may have been
        // garbage collected
        if(anchored) {
            anchored.release();
        }
        return true;
    });
}
