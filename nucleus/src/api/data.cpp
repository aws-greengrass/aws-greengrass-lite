#include "api_error_trap.hpp"
#include "data/shared_buffer.hpp"
#include "data/shared_list.hpp"
#include "data/shared_struct.hpp"
#include "scope/context_full.hpp"
#include <cpp_api.hpp>

using namespace data;

/**
 * Retrieve symbol from a string. This function is guaranteed to succeed or terminate the process.
 * Expected reasons for termination are: 1/ Bad pointer (which will result in corrupted symbols), or
 * 2/ Out of memory Termination is the right thing as it allows a watchdog to restart the process.
 */
ggapiSymbol ggapiGetSymbol(const char *bytes, size_t len) noexcept {
    try {
        return scope::context()->intern(std::string_view{bytes, len}).asInt();
    } catch(...) {
        std::terminate(); // any string table put errors would be a critical
                          // error requiring termination
    }
}

/**
 * Extract a string from symbol. Buffer is NOT zero-terminated, following C++ semantics. Caller is
 * responsible for zero-terminating buffer if desired.
 *
 * @param symbolInt Integer ID of symbol
 * @param bytes  Buffer to fill
 * @param len    Length of buffer
 * @param pFilled Number of bytes into buffer (size of validity)
 * @param pLength Required length of buffer
 * @return 0 on success. Non-zero on error other than buffer too small.
 */
ggapiErrorKind ggapiGetSymbolString(
    ggapiSymbol symbolInt,
    ggapiByteBuffer bytes,
    ggapiMaxLen len,
    ggapiDataLen *pFilled,
    ggapiDataLen *pLength) noexcept {
    return apiImpl::catchErrorToKind([symbolInt, bytes, len, pFilled, pLength]() {
        Symbol symbol = scope::context()->symbolFromInt(symbolInt);
        std::string s{symbol.toString()};
        std::string::size_type fillLen = s.length();
        *pFilled = 0; // ensures value meaningful in case of exception
        *pLength = s.length();
        if(fillLen > len) {
            fillLen = len;
        }
        util::Span span(bytes, fillLen);
        *pFilled = span.copyFrom(s.begin(), s.end());
    });
}

ggapiErrorKind ggapiGetSymbolStringLen(ggapiSymbol symbolInt, ggapiDataLen *pLength) noexcept {
    return apiImpl::catchErrorToKind([symbolInt, pLength]() {
        Symbol symbol = scope::context()->symbolFromInt(symbolInt);
        std::string s{symbol.toString()};
        *pLength = s.length();
    });
}

ggapiErrorKind ggapiCreateStruct(ggapiObjHandle *pHandle) noexcept {
    return apiImpl::catchErrorToKind([pHandle]() {
        auto obj = scope::makeObject<SharedStruct>();
        *pHandle = scope::asIntHandle(obj);
    });
}

ggapiErrorKind ggapiCreateList(ggapiObjHandle *pHandle) noexcept {
    return apiImpl::catchErrorToKind([pHandle]() {
        auto obj = scope::makeObject<SharedList>();
        *pHandle = scope::asIntHandle(obj);
    });
}

ggapiErrorKind ggapiCreateBuffer(ggapiObjHandle *pHandle) noexcept {
    return apiImpl::catchErrorToKind([pHandle]() {
        auto obj = scope::makeObject<SharedBuffer>();
        *pHandle = scope::asIntHandle(obj);
    });
}

ggapiErrorKind ggapiIsScalar(ggapiObjHandle handle, ggapiBool *pBool) noexcept {
    return apiImpl::catchErrorToKind([handle, pBool]() {
        auto ss{scope::context()->objFromInt(handle)};
        auto boxed = std::dynamic_pointer_cast<Boxed>(ss);
        apiImpl::setBool(pBool, boxed && boxed->get().isScalar());
    });
}

ggapiErrorKind ggapiIsContainer(ggapiObjHandle handle, ggapiBool *pBool) noexcept {
    return apiImpl::catchErrorToKind([handle, pBool]() {
        auto ss{scope::context()->objFromInt(handle)};
        apiImpl::setBool(pBool, std::dynamic_pointer_cast<ContainerModelBase>(ss) != nullptr);
    });
}

ggapiErrorKind ggapiIsStruct(ggapiObjHandle handle, ggapiBool *pBool) noexcept {
    return apiImpl::catchErrorToKind([handle, pBool]() {
        auto ss{scope::context()->objFromInt(handle)};
        apiImpl::setBool(pBool, std::dynamic_pointer_cast<StructModelBase>(ss) != nullptr);
    });
}

ggapiErrorKind ggapiIsList(ggapiObjHandle handle, ggapiBool *pBool) noexcept {
    return apiImpl::catchErrorToKind([handle, pBool]() {
        auto ss{scope::context()->objFromInt(handle)};
        apiImpl::setBool(pBool, std::dynamic_pointer_cast<ListModelBase>(ss) != nullptr);
    });
}

ggapiErrorKind ggapiIsBuffer(ggapiObjHandle handle, ggapiBool *pBool) noexcept {
    return apiImpl::catchErrorToKind([handle, pBool]() {
        auto ss{scope::context()->objFromInt(handle)};
        apiImpl::setBool(pBool, std::dynamic_pointer_cast<SharedBuffer>(ss) != nullptr);
    });
}

ggapiErrorKind ggapiIsScope(ggapiObjHandle handle, ggapiBool *pBool) noexcept {
    return apiImpl::catchErrorToKind([handle, pBool]() {
        auto ss{scope::context()->objFromInt(handle)};
        apiImpl::setBool(pBool, std::dynamic_pointer_cast<TrackingScope>(ss) != nullptr);
    });
}

bool ggapiIsSameObject(uint32_t handle1, uint32_t handle2) noexcept {
    // Two different handles can refer to same object
    return ggapi::trapErrorReturn<bool>([handle1, handle2]() {
        auto context = scope::context();
        auto obj1{context->objFromInt(handle1)};
        auto obj2{context->objFromInt(handle2)};
        return obj1 == obj2;
    });
}

uint32_t ggapiBoxBool(bool value) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([value]() {
        auto context = scope::context();
        auto boxed = data::Boxed::box(context, value);
        return scope::asIntHandle(boxed);
    });
}

uint32_t ggapiBoxInt64(uint64_t value) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([value]() {
        auto context = scope::context();
        auto boxed = data::Boxed::box(context, value);
        return scope::asIntHandle(boxed);
    });
}

uint32_t ggapiBoxFloat64(double value) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([value]() {
        auto context = scope::context();
        auto boxed = data::Boxed::box(context, value);
        return scope::asIntHandle(boxed);
    });
}

uint32_t ggapiBoxString(const char *bytes, size_t len) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([bytes, len]() {
        auto context = scope::context();
        auto boxed = data::Boxed::box(context, std::string_view(bytes, len));
        return scope::asIntHandle(boxed);
    });
}

uint32_t ggapiBoxSymbol(uint32_t symValInt) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([symValInt]() {
        auto context = scope::context();
        auto value = context->symbolFromInt(symValInt);
        auto boxed = data::Boxed::box(context, value);
        return scope::asIntHandle(boxed);
    });
}

uint32_t ggapiBoxHandle(uint32_t handle) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([handle]() -> uint32_t {
        auto context = scope::context();
        if(handle == 0) {
            return 0;
        }
        auto value = context->objFromInt(handle);
        auto boxed = data::Boxed::box(context, value);
        return scope::asIntHandle(boxed);
    });
}

bool ggapiUnboxBool(uint32_t handle) noexcept {
    return ggapi::trapErrorReturn<bool>([handle]() {
        auto context = scope::context();
        auto obj = context->objFromInt<Boxed>(handle);
        return obj->get().getBool();
    });
}

uint64_t ggapiUnboxInt64(uint32_t handle) noexcept {
    return ggapi::trapErrorReturn<uint64_t>([handle]() {
        auto context = scope::context();
        auto obj = context->objFromInt<Boxed>(handle);
        return obj->get().getInt();
    });
}

double ggapiUnboxFloat64(uint32_t handle) noexcept {
    return ggapi::trapErrorReturn<double>([handle]() {
        auto context = scope::context();
        auto obj = context->objFromInt<Boxed>(handle);
        return obj->get().getDouble();
    });
}

size_t ggapiUnboxStringLen(uint32_t handle) noexcept {
    return ggapi::trapErrorReturn<size_t>([handle]() {
        auto context = scope::context();
        auto obj = context->objFromInt<Boxed>(handle);
        return obj->get().getStringLen();
    });
}

size_t ggapiUnboxString(uint32_t handle, char *buffer, size_t buflen) noexcept {
    util::Span span(buffer, buflen);
    return ggapi::trapErrorReturn<size_t>([handle, span]() {
        auto context = scope::context();
        auto obj = context->objFromInt<Boxed>(handle);
        return obj->get().getString(span);
    });
}

uint32_t ggapiUnboxHandle(uint32_t handle) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([handle]() {
        auto context = scope::context();
        auto obj = context->objFromInt(handle);
        auto boxed = std::dynamic_pointer_cast<Boxed>(obj);
        if(boxed) {
            obj = boxed->get().getObject();
        } else {
            // Not an error, just localize the handle provided
        }
        return scope::asIntHandle(obj);
    });
}

bool ggapiStructPutBool(uint32_t structHandle, uint32_t keyInt, bool value) noexcept {
    return ggapi::trapErrorReturn<bool>([structHandle, keyInt, value]() {
        auto context = scope::context();
        auto ss{context->objFromInt<StructModelBase>(structHandle)};
        Symbol key = context->symbolFromInt(keyInt);
        StructElement newElement{value};
        ss->put(key, newElement);
        return true;
    });
}

bool ggapiListPutBool(uint32_t listHandle, int32_t idx, bool value) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, value]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        StructElement newElement{value};
        ss->put(idx, newElement);
        return true;
    });
}

bool ggapiListInsertBool(uint32_t listHandle, int32_t idx, bool value) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, value]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        StructElement newElement{value};
        ss->insert(idx, newElement);
        return true;
    });
}

bool ggapiStructPutInt64(uint32_t structHandle, uint32_t keyInt, uint64_t value) noexcept {
    return ggapi::trapErrorReturn<bool>([structHandle, keyInt, value]() {
        auto context = scope::context();
        auto ss{context->objFromInt<StructModelBase>(structHandle)};
        Symbol key = context->symbolFromInt(keyInt);
        StructElement newElement{value};
        ss->put(key, newElement);
        return true;
    });
}

bool ggapiListPutInt64(uint32_t listHandle, int32_t idx, uint64_t value) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, value]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        StructElement newElement{value};
        ss->put(idx, newElement);
        return true;
    });
}

bool ggapiListInsertInt64(uint32_t listHandle, int32_t idx, uint64_t value) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, value]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        StructElement newElement{value};
        ss->insert(idx, newElement);
        return true;
    });
}

bool ggapiStructPutFloat64(uint32_t structHandle, uint32_t keyInt, double value) noexcept {
    return ggapi::trapErrorReturn<bool>([structHandle, keyInt, value]() {
        auto context = scope::context();
        auto ss{context->objFromInt<StructModelBase>(structHandle)};
        Symbol key = context->symbolFromInt(keyInt);
        StructElement newElement{value};
        ss->put(key, newElement);
        return true;
    });
}

bool ggapiListPutFloat64(uint32_t listHandle, int32_t idx, double value) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, value]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        StructElement newElement{value};
        ss->put(idx, newElement);
        return true;
    });
}

bool ggapiListInsertFloat64(uint32_t listHandle, int32_t idx, double value) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, value]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        StructElement newElement{value};
        ss->insert(idx, newElement);
        return true;
    });
}

static StructElement optimizeString(const scope::UsingContext &context, std::string_view str) {
    // Opportunistic - if string matches an existing ordinal, use it,
    // otherwise just store as a string as not to pollute string ord table
    Symbol ord = context->symbols().testAndGetSymbol(str);
    if(ord) {
        return {ord};
    } else {
        return {str};
    }
}

bool ggapiStructPutString(
    uint32_t structHandle, uint32_t keyInt, const char *bytes, size_t len) noexcept {
    return ggapi::trapErrorReturn<bool>([structHandle, keyInt, bytes, len]() {
        auto context = scope::context();
        auto ss{context->objFromInt<StructModelBase>(structHandle)};
        Symbol key = context->symbolFromInt(keyInt);
        StructElement newElement{optimizeString(context, std::string_view(bytes, len))};
        ss->put(key, newElement);
        return true;
    });
}

bool ggapiListPutString(uint32_t listHandle, int32_t idx, const char *bytes, size_t len) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, bytes, len]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        StructElement newElement{optimizeString(context, std::string_view(bytes, len))};
        ss->put(idx, newElement);
        return true;
    });
}

bool ggapiListInsertString(
    uint32_t listHandle, int32_t idx, const char *bytes, size_t len) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, bytes, len]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        StructElement newElement{optimizeString(context, std::string_view(bytes, len))};
        ss->insert(idx, newElement);
        return true;
    });
}

bool ggapiStructPutSymbol(uint32_t listHandle, uint32_t symInt, uint32_t symValInt) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, symInt, symValInt]() {
        auto context = scope::context();
        auto ss{context->objFromInt<StructModelBase>(listHandle)};
        Symbol key = context->symbolFromInt(symInt);
        Symbol value = context->symbolFromInt(symValInt);
        StructElement newElement{value};
        ss->put(key, newElement);
        return true;
    });
}

bool ggapiListPutSymbol(uint32_t listHandle, int32_t idx, uint32_t symValInt) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, symValInt]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        Symbol value = context->symbolFromInt(symValInt);
        StructElement newElement{value};
        ss->put(idx, newElement);
        return true;
    });
}

bool ggapiListInsertSymbol(uint32_t listHandle, int32_t idx, uint32_t symVal) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, symVal]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        Symbol valueH = context->symbolFromInt(symVal);
        StructElement newElement{valueH};
        ss->insert(idx, newElement);
        return true;
    });
}

bool ggapiStructPutHandle(uint32_t structHandle, uint32_t keyInt, uint32_t nestedHandle) noexcept {
    return ggapi::trapErrorReturn<bool>([structHandle, keyInt, nestedHandle]() {
        auto context = scope::context();
        auto ss{context->objFromInt<StructModelBase>(structHandle)};
        auto s2{context->objFromInt(nestedHandle)};
        Symbol key = context->symbolFromInt(keyInt);
        StructElement newElement{s2};
        ss->put(key, newElement);
        return true;
    });
}

bool ggapiListPutHandle(uint32_t listHandle, int32_t idx, uint32_t nestedHandle) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, nestedHandle]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        auto s2{context->objFromInt(nestedHandle)};
        StructElement newElement{s2};
        ss->put(idx, newElement);
        return true;
    });
}

bool ggapiListInsertHandle(uint32_t listHandle, int32_t idx, uint32_t nestedHandle) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx, nestedHandle]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        auto s2{context->handleFromInt(nestedHandle).toObject<TrackedObject>()};
        StructElement newElement{s2};
        ss->insert(idx, newElement);
        return true;
    });
}

bool ggapiBufferPut(uint32_t bufHandle, int32_t idx, const char *bytes, uint32_t len) noexcept {
    return ggapi::trapErrorReturn<bool>([bufHandle, idx, bytes, len]() {
        auto context = scope::context();
        auto ss{context->objFromInt<SharedBuffer>(bufHandle)};
        ConstMemoryView buffer{bytes, len};
        ss->put(idx, buffer);
        return true;
    });
}

bool ggapiBufferInsert(uint32_t bufHandle, int32_t idx, const char *bytes, uint32_t len) noexcept {
    return ggapi::trapErrorReturn<bool>([bufHandle, idx, bytes, len]() {
        auto context = scope::context();
        auto ss{context->objFromInt<SharedBuffer>(bufHandle)};
        ConstMemoryView buffer{bytes, len};
        ss->insert(idx, buffer);
        return true;
    });
}

bool ggapiStructHasKey(uint32_t structHandle, uint32_t keyInt) noexcept {
    return ggapi::trapErrorReturn<bool>([structHandle, keyInt]() {
        auto context = scope::context();
        auto ss{context->objFromInt<StructModelBase>(structHandle)};
        Symbol key = context->symbolFromInt(keyInt);
        return ss->hasKey(key);
    });
}

ggapiErrorKind ggapiStructFoldKey(
    ggapiObjHandle structHandle, ggapiSymbol key, ggapiSymbol *retKey) noexcept {
    return apiImpl::catchErrorToKind([structHandle, key, retKey]() -> void {
        auto context = scope::context();
        auto ss{context->objFromInt<StructModelBase>(structHandle)};
        Symbol keySym = context->symbolFromInt(key);
        Symbol keyOut = ss->foldKey(keySym, true);
        *retKey = keyOut.asInt();
    });
}

uint32_t ggapiStructKeys(uint32_t structHandle) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([structHandle]() {
        auto context = scope::context();
        auto ss{context->objFromInt<StructModelBase>(structHandle)};
        return scope::asIntHandle(ss->getKeysAsList());
    });
}

uint32_t ggapiGetSize(uint32_t containerHandle) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([containerHandle]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ContainerModelBase>(containerHandle)};
        return ss->size();
    });
}

bool ggapiIsEmpty(uint32_t containerHandle) noexcept {
    return ggapi::trapErrorReturn<bool>([containerHandle]() {
        if(!containerHandle) {
            return true;
        }
        auto context = scope::context();
        auto ss{context->objFromInt<ContainerModelBase>(containerHandle)};
        return ss->empty();
    });
}

ggapiErrorKind ggapiCloneContainer(ggapiObjHandle objHandle, ggapiObjHandle *retObject) noexcept {
    return apiImpl::catchErrorToKind([objHandle, retObject]() -> void {
        auto context = scope::context();
        auto obj{context->objFromInt<data::ContainerModelBase>(objHandle)};
        auto copy = obj->clone();
        *retObject = scope::asIntHandle(copy);
    });
}

ggapiErrorKind ggapiStructCreateForChild(
    ggapiObjHandle objHandle, ggapiObjHandle *retObject) noexcept {
    return apiImpl::catchErrorToKind([objHandle, retObject]() -> void {
        auto context = scope::context();
        auto obj{context->objFromInt<data::StructModelBase>(objHandle)};
        auto newStruct = obj->createForChild();
        *retObject = scope::asIntHandle(newStruct);
    });
}

bool ggapiStructGetBool(uint32_t structHandle, uint32_t keyInt) noexcept {
    return ggapi::trapErrorReturn<bool>([structHandle, keyInt]() {
        auto context = scope::context();
        auto ss{context->objFromInt<StructModelBase>(structHandle)};
        Symbol key = context->symbolFromInt(keyInt);
        return ss->get(key).getBool();
    });
}

bool ggapiListGetBool(uint32_t listHandle, int32_t idx) noexcept {
    return ggapi::trapErrorReturn<bool>([listHandle, idx]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        return ss->get(idx).getBool();
    });
}

uint64_t ggapiStructGetInt64(uint32_t structHandle, uint32_t keyInt) noexcept {
    return ggapi::trapErrorReturn<uint64_t>([structHandle, keyInt]() {
        auto context = scope::context();
        auto ss{context->objFromInt<StructModelBase>(structHandle)};
        Symbol key = context->symbolFromInt(keyInt);
        return static_cast<uint64_t>(ss->get(key));
    });
}

uint64_t ggapiListGetInt64(uint32_t listHandle, int32_t idx) noexcept {
    return ggapi::trapErrorReturn<uint64_t>([listHandle, idx]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        return static_cast<uint64_t>(ss->get(idx));
    });
}

double ggapiStructGetFloat64(uint32_t structHandle, uint32_t keyInt) noexcept {
    return ggapi::trapErrorReturn<double>([structHandle, keyInt]() {
        auto context = scope::context();
        auto ss{context->objFromInt<StructModelBase>(structHandle)};
        Symbol key = context->symbolFromInt(keyInt);
        return static_cast<double>(ss->get(key));
    });
}

double ggapiListGetFloat64(uint32_t listHandle, int32_t idx) noexcept {
    return ggapi::trapErrorReturn<double>([listHandle, idx]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        return static_cast<double>(ss->get(idx));
    });
}

uint32_t ggapiStructGetHandle(uint32_t structHandle, uint32_t keyInt) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([structHandle, keyInt]() {
        auto context = scope::context();
        auto ss{context->objFromInt<StructModelBase>(structHandle)};
        Symbol key = context->symbolFromInt(keyInt);
        auto v = ss->get(key).getObject();
        return scope::asIntHandle(v);
    });
}

uint32_t ggapiListGetHandle(uint32_t listHandle, int32_t idx) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([listHandle, idx]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        auto v = ss->get(idx).getObject();
        return scope::asIntHandle(v);
    });
}

size_t ggapiStructGetStringLen(uint32_t structHandle, uint32_t keyInt) noexcept {
    return ggapi::trapErrorReturn<size_t>([structHandle, keyInt]() {
        auto context = scope::context();
        auto ss{context->objFromInt<StructModelBase>(structHandle)};
        Symbol key = context->symbolFromInt(keyInt);
        return ss->get(key).getStringLen();
    });
}

size_t ggapiStructGetString(
    uint32_t structHandle, uint32_t keyInt, char *buffer, size_t buflen) noexcept {
    util::Span span(buffer, buflen);
    return ggapi::trapErrorReturn<size_t>([structHandle, keyInt, span]() {
        auto context = scope::context();
        auto ss{context->objFromInt<StructModelBase>(structHandle)};
        Symbol key = context->symbolFromInt(keyInt);
        return ss->get(key).getString(span);
    });
}

size_t ggapiListGetStringLen(uint32_t listHandle, int32_t idx) noexcept {
    return ggapi::trapErrorReturn<size_t>([listHandle, idx]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        return ss->get(idx).getStringLen();
    });
}

size_t ggapiListGetString(uint32_t listHandle, int32_t idx, char *buffer, size_t buflen) noexcept {
    util::Span span(buffer, buflen);
    return ggapi::trapErrorReturn<size_t>([listHandle, idx, span]() {
        auto context = scope::context();
        auto ss{context->objFromInt<ListModelBase>(listHandle)};
        return ss->get(idx).getString(span);
    });
}

uint32_t ggapiBufferGet(uint32_t bufHandle, int32_t idx, char *bytes, uint32_t len) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([bufHandle, idx, bytes, len]() {
        auto context = scope::context();
        auto ss{context->objFromInt<SharedBuffer>(bufHandle)};
        MemoryView buffer{bytes, len};
        return ss->get(idx, buffer);
    });
}

/**
 * Allocate a temporary handle, needed for returning handles in callbacks.
 * @param handleIn Handle of existing object
 * @param pHandle New temporary handle
 * @return error if failed
 */
ggapiErrorKind ggapiTempHandle(ggapiObjHandle handleIn, ggapiObjHandle *pHandle) noexcept {
    return apiImpl::catchErrorToKind([handleIn, pHandle]() {
        *pHandle = 0;
        auto context = scope::context();
        auto obj = context->objFromInt(handleIn);
        if(!obj) {
            return;
        }
        auto root = scope::thread()->getTempRoot();
        if(!root) {
            throw std::logic_error("No temporary root allocated in thread");
        }
        *pHandle = context->handles().create(obj, *root).asInt();
    });
}

/**
 * Duplicate a handle. The duplicated handle is bound to plugin context.
 * @param handleIn Handle of existing object
 * @param pHandle New module-bound handle
 * @return error if failed
 */
ggapiErrorKind ggapiDupHandle(ggapiObjHandle handleIn, ggapiObjHandle *pHandle) noexcept {
    return apiImpl::catchErrorToKind([handleIn, pHandle]() {
        *pHandle = 0;
        auto context = scope::context();
        auto obj = context->objFromInt(handleIn);
        if(!obj) {
            return;
        }
        *pHandle = scope::asIntHandle(obj);
    });
}

/**
 * Release a handle, de-ref object managed by handle.
 * @param objectHandle Handle to release (becomes invalid after call)
 */
ggapiErrorKind ggapiReleaseHandle(uint32_t objectHandle) noexcept {
    return apiImpl::catchErrorToKind([objectHandle]() {
        if(objectHandle) {
            auto handle = scope::context()->handleFromInt(objectHandle);
            handle.release();
        }
    });
}

/**
 * Close a handle - meaning of close depends on the object
 * @param objectHandle Handle to close (becomes invalid after call)
 */
ggapiErrorKind ggapiCloseHandle(uint32_t objectHandle) noexcept {
    return apiImpl::catchErrorToKind([objectHandle]() {
        if(objectHandle) {
            auto obj = scope::context()->objFromInt(objectHandle);
            obj->close();
        }
    });
}

uint32_t ggapiToJson(uint32_t objectHandle) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([objectHandle]() {
        auto context = scope::context();
        auto container = context->objFromInt<data::ContainerModelBase>(objectHandle);
        auto buffer = container->toJson();
        return scope::asIntHandle(buffer);
    });
}

uint32_t ggapiFromJson(uint32_t bufferHandle) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([bufferHandle]() {
        // impl
        auto context = scope::context();
        auto buffer = context->objFromInt<data::SharedBuffer>(bufferHandle);
        auto container = buffer->parseJson();
        return scope::asIntHandle(container);
    });
}

uint32_t ggapiToYaml(uint32_t objectHandle) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([objectHandle]() {
        auto context = scope::context();
        auto container = context->objFromInt<data::ContainerModelBase>(objectHandle);
        auto buffer = container->toYaml();
        return scope::asIntHandle(buffer);
    });
}

uint32_t ggapiFromYaml(uint32_t bufferHandle) noexcept {
    return ggapi::trapErrorReturn<uint32_t>([bufferHandle]() {
        auto context = scope::context();
        auto buffer = context->objFromInt<data::SharedBuffer>(bufferHandle);
        auto container = buffer->parseYaml();
        return scope::asIntHandle(container);
    });
}
