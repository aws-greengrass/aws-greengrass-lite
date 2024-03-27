#ifndef GG_PLUGIN_API
#define GG_PLUGIN_API

#include <stddef.h> // NOLINT(modernize-deprecated-headers)
#include <stdint.h> // NOLINT(modernize-deprecated-headers)

#ifdef __cplusplus
#define NOEXCEPT noexcept
#else
#define NOEXCEPT
#endif

#if defined(_WIN32)
#define IMPORT __declspec(dllimport)
#define EXPORT __declspec(dllexport)
#else
#define IMPORT
#define EXPORT __attribute__((visibility("default")))
#endif
#if defined(EXPORT_API)
#define IMPEXP EXPORT
#else
#define IMPEXP IMPORT
#endif

typedef uint32_t ggapiErrorKind; // Symbol representing kind of error, 0 = success
typedef uint32_t ggapiObjHandle; // Generic Handle to an object, 0 = unset
typedef uint32_t ggapiSymbol; // Generic Symbol, 0 = unset
typedef uint32_t ggapiBool; // 0 = FALSE, non-zero = TRUE
typedef char *ggapiByteBuffer; // Uninitialized buffer to be filled with data
typedef const char *ggapiCountedString; // Pointer to a string, string is not null terminated
typedef size_t ggapiMaxLen; // Length of a buffer that can be filled
typedef size_t ggapiDataLen; // Length of valid data in a buffer / string length
typedef uintptr_t ggapiContext; // Round-trip data never interpreted by Nucleus

//
// When changing/reviewing callback structures:
// Fields may be appended, must never be removed
// Deprecated fields must be nulled (0)
// sizeof(struct) denotes version. That is, if
// a structure adds a new field, it is implicitly bigger,
// and implicitly a new version
// Note, returned handles need to be "temporary"
//

struct ggapiTopicCallbackData {
    ggapiSymbol topicSymbol;
    ggapiObjHandle data; // Container
    ggapiObjHandle ret; // Return value
};

struct ggapiAsyncCallbackData {
    uint8_t _dummy;
};

struct ggapiFutureCallbackData {
    ggapiObjHandle futureHandle;
};

struct ggapiLifecycleCallbackData {
    ggapiObjHandle moduleHandle;
    ggapiSymbol phaseSymbol;
    ggapiObjHandle dataStruct;
    uint32_t retWasHandled; // Out, holds non-zero if handled, 0 if not (forced 32-bit aligned)
};

struct ggapiChannelListenCallbackData {
    ggapiObjHandle data;
};

struct ggapiChannelCloseCallbackData {
    uint8_t _dummy;
};

typedef ggapiErrorKind (*ggapiGenericCallback)(
    ggapiContext callbackContext,
    ggapiSymbol callbackType,
    ggapiDataLen callbackDataSize,
    void *callbackData) NOEXCEPT;

typedef ggapiErrorKind GgapiLifecycleFn(
    ggapiObjHandle moduleHandle,
    ggapiSymbol phase,
    ggapiObjHandle data,
    bool *pWasHandled) NOEXCEPT;

[[maybe_unused]] EXPORT GgapiLifecycleFn greengrass_lifecycle;

IMPEXP ggapiErrorKind
ggapiSetError(ggapiErrorKind kind, ggapiCountedString what, ggapiDataLen len) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiGetErrorKind() NOEXCEPT;
IMPEXP const char *ggapiGetErrorWhat() NOEXCEPT;
IMPEXP ggapiSymbol ggapiGetSymbol(ggapiCountedString bytes, ggapiDataLen len) NOEXCEPT;

IMPEXP ggapiErrorKind ggapiGetSymbolString(
    ggapiSymbol symbolInt,
    ggapiByteBuffer bytes,
    ggapiMaxLen len,
    ggapiDataLen *pFilled,
    ggapiDataLen *pLength) NOEXCEPT;
IMPEXP ggapiErrorKind
ggapiGetSymbolStringLen(ggapiSymbol symbolInt, ggapiDataLen *pLength) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiCreateStruct(ggapiObjHandle *pHandle) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiCreateList(ggapiObjHandle *pHandle) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiCreateBuffer(ggapiObjHandle *pHandle) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiCreateChannel(ggapiObjHandle *pHandle) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiCreatePromise(ggapiObjHandle *pHandle) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiIsContainer(ggapiObjHandle handle, ggapiBool *pBool) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiIsScalar(ggapiObjHandle handle, ggapiBool *pBool) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiIsStruct(ggapiObjHandle handle, ggapiBool *pBool) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiIsList(ggapiObjHandle handle, ggapiBool *pBool) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiIsBuffer(ggapiObjHandle handle, ggapiBool *pBool) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiIsChannel(ggapiObjHandle handle, ggapiBool *pBool) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiIsSubscription(ggapiObjHandle handle, ggapiBool *pBool) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiIsFuture(ggapiObjHandle handle, ggapiBool *pBool) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiIsPromise(ggapiObjHandle handle, ggapiBool *pBool) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiIsScope(ggapiObjHandle handle, ggapiBool *pBool) NOEXCEPT;
IMPEXP bool ggapiIsSameObject(uint32_t handle1, uint32_t handle2) NOEXCEPT;
IMPEXP uint32_t ggapiBoxBool(bool value) NOEXCEPT;
IMPEXP uint32_t ggapiBoxInt64(uint64_t value) NOEXCEPT;
IMPEXP uint32_t ggapiBoxFloat64(double value) NOEXCEPT;
IMPEXP uint32_t ggapiBoxString(const char *bytes, size_t len) NOEXCEPT;
IMPEXP uint32_t ggapiBoxSymbol(uint32_t symValInt) NOEXCEPT;
IMPEXP uint32_t ggapiBoxHandle(uint32_t handle) NOEXCEPT;
IMPEXP bool ggapiUnboxBool(uint32_t handle) NOEXCEPT;
IMPEXP uint64_t ggapiUnboxInt64(uint32_t handle) NOEXCEPT;
IMPEXP double ggapiUnboxFloat64(uint32_t handle) NOEXCEPT;
IMPEXP size_t ggapiUnboxString(uint32_t handle, char *buffer, size_t buflen) NOEXCEPT;
IMPEXP size_t ggapiUnboxStringLen(uint32_t handle) NOEXCEPT;
IMPEXP uint32_t ggapiUnboxSymbol(uint32_t handle) NOEXCEPT;
IMPEXP uint32_t ggapiUnboxHandle(uint32_t handle) NOEXCEPT;
IMPEXP bool ggapiStructPutBool(uint32_t structHandle, uint32_t keyInt, bool value) NOEXCEPT;
IMPEXP bool ggapiStructPutInt64(uint32_t structHandle, uint32_t symInt, uint64_t value) NOEXCEPT;
IMPEXP bool ggapiStructPutFloat64(uint32_t structHandle, uint32_t symInt, double value) NOEXCEPT;
IMPEXP bool ggapiStructPutString(
    uint32_t structHandle, uint32_t symInt, const char *bytes, size_t len) NOEXCEPT;
IMPEXP bool ggapiStructPutSymbol(uint32_t listHandle, uint32_t symInt, uint32_t symValInt) NOEXCEPT;
IMPEXP bool ggapiStructPutHandle(
    uint32_t structHandle, uint32_t symInt, uint32_t nestedHandle) NOEXCEPT;
IMPEXP bool ggapiStructHasKey(uint32_t structHandle, uint32_t keyInt) NOEXCEPT;
IMPEXP ggapiErrorKind
ggapiStructFoldKey(ggapiObjHandle structHandle, ggapiSymbol key, ggapiSymbol *retKey) NOEXCEPT;
IMPEXP uint32_t ggapiStructKeys(uint32_t structHandle) NOEXCEPT;
IMPEXP bool ggapiStructGetBool(uint32_t structHandle, uint32_t keyInt) NOEXCEPT;
IMPEXP uint64_t ggapiStructGetInt64(uint32_t structHandle, uint32_t keyInt) NOEXCEPT;
IMPEXP double ggapiStructGetFloat64(uint32_t structHandle, uint32_t keyInt) NOEXCEPT;
IMPEXP size_t ggapiStructGetStringLen(uint32_t structHandle, uint32_t keyInt) NOEXCEPT;
IMPEXP size_t
ggapiStructGetString(uint32_t structHandle, uint32_t symInt, char *buffer, size_t buflen) NOEXCEPT;
IMPEXP uint32_t ggapiStructGetHandle(uint32_t structHandle, uint32_t keyInt) NOEXCEPT;
IMPEXP ggapiErrorKind
ggapiCloneContainer(ggapiObjHandle objHandle, ggapiObjHandle *retObject) NOEXCEPT;
IMPEXP ggapiErrorKind
ggapiStructCreateForChild(ggapiObjHandle objHandle, ggapiObjHandle *retObject) NOEXCEPT;
IMPEXP bool ggapiListPutBool(uint32_t listHandle, int32_t idx, bool value) NOEXCEPT;
IMPEXP bool ggapiListPutInt64(uint32_t listHandle, int32_t idx, uint64_t value) NOEXCEPT;
IMPEXP bool ggapiListPutFloat64(uint32_t listHandle, int32_t idx, double value) NOEXCEPT;
IMPEXP bool ggapiListPutString(
    uint32_t listHandle, int32_t idx, const char *bytes, size_t len) NOEXCEPT;
IMPEXP bool ggapiListPutSymbol(uint32_t listHandle, int32_t idx, uint32_t symValInt) NOEXCEPT;
IMPEXP bool ggapiListPutHandle(uint32_t listHandle, int32_t idx, uint32_t nestedHandle) NOEXCEPT;
IMPEXP bool ggapiListInsertBool(uint32_t listHandle, int32_t idx, bool value) NOEXCEPT;
IMPEXP bool ggapiListInsertInt64(uint32_t listHandle, int32_t idx, uint64_t value) NOEXCEPT;
IMPEXP bool ggapiListInsertFloat64(uint32_t listHandle, int32_t idx, double value) NOEXCEPT;
IMPEXP bool ggapiListInsertString(
    uint32_t listHandle, int32_t idx, const char *bytes, size_t len) NOEXCEPT;
IMPEXP bool ggapiListInsertSymbol(uint32_t listHandle, int32_t idx, uint32_t symVal) NOEXCEPT;
IMPEXP bool ggapiListInsertHandle(uint32_t listHandle, int32_t idx, uint32_t nestedHandle) NOEXCEPT;
IMPEXP bool ggapiListGetBool(uint32_t structHandle, int32_t idx) NOEXCEPT;
IMPEXP uint64_t ggapiListGetInt64(uint32_t structHandle, int32_t idx) NOEXCEPT;
IMPEXP double ggapiListGetFloat64(uint32_t structHandle, int32_t idx) NOEXCEPT;
IMPEXP size_t ggapiListGetStringLen(uint32_t listHandle, int32_t idx) NOEXCEPT;
IMPEXP size_t
ggapiListGetString(uint32_t structHandle, int32_t idx, char *buffer, size_t buflen) NOEXCEPT;
IMPEXP uint32_t ggapiListGetHandle(uint32_t listHandle, int32_t idx) NOEXCEPT;
IMPEXP bool ggapiBufferPut(
    uint32_t bufHandle, int32_t idx, const char *buffer, uint32_t buflen) NOEXCEPT;
IMPEXP bool ggapiBufferInsert(
    uint32_t bufHandle, int32_t idx, const char *buffer, uint32_t buflen) NOEXCEPT;
IMPEXP uint32_t
ggapiBufferGet(uint32_t listHandle, int32_t idx, char *buffer, uint32_t buflen) NOEXCEPT;
IMPEXP bool ggapiBufferResize(uint32_t structHandle, uint32_t newSize) NOEXCEPT;
IMPEXP bool ggapiIsEmpty(uint32_t containerHandle) NOEXCEPT;
IMPEXP uint32_t ggapiGetSize(uint32_t containerHandle) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiTempHandle(ggapiObjHandle handleIn, ggapiObjHandle *pHandle) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiDupHandle(ggapiObjHandle handleIn, ggapiObjHandle *pHandle) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiReleaseHandle(uint32_t objectHandle) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiCloseHandle(uint32_t objectHandle) NOEXCEPT;
IMPEXP uint32_t ggapiToJson(uint32_t containerHandle) NOEXCEPT;
IMPEXP uint32_t ggapiFromJson(uint32_t bufferHandle) NOEXCEPT;
IMPEXP uint32_t ggapiToYaml(uint32_t containerHandle) NOEXCEPT;
IMPEXP uint32_t ggapiFromYaml(uint32_t bufferHandle) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiGetCurrentModule(ggapiObjHandle *pHandle) NOEXCEPT;
IMPEXP ggapiErrorKind
ggapiPromiseSetValue(ggapiObjHandle promiseHandle, ggapiObjHandle newValue) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiPromiseSetError(
    ggapiObjHandle promiseHandle, ggapiSymbol errorKind, const char *str, uint32_t strlen) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiPromiseCancel(ggapiObjHandle promiseHandle) NOEXCEPT;
IMPEXP ggapiErrorKind
ggapiFutureGetValue(ggapiObjHandle futureHandle, ggapiObjHandle *outValue) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiFutureIsValid(ggapiObjHandle futureHandle, ggapiBool *outValue) NOEXCEPT;
IMPEXP ggapiErrorKind
ggapiFutureWait(ggapiObjHandle futureHandle, int32_t timeout, ggapiBool *outValue) NOEXCEPT;
IMPEXP ggapiErrorKind
ggapiFutureFromPromise(ggapiObjHandle promiseHandle, ggapiObjHandle *outFuture) NOEXCEPT;
IMPEXP ggapiErrorKind
ggapiFutureAddCallback(ggapiObjHandle futureHandle, ggapiObjHandle callbackHandle) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiCallAsync(ggapiObjHandle callbackHandle, uint32_t delay) NOEXCEPT;
IMPEXP ggapiErrorKind
ggapiCallDirect(ggapiObjHandle target, ggapiObjHandle data, ggapiObjHandle *outFuture) NOEXCEPT;
IMPEXP ggapiErrorKind
ggapiCallTopicFirst(ggapiSymbol topic, ggapiObjHandle data, ggapiObjHandle *outFuture) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiCallTopicAll(
    ggapiSymbol topic, ggapiObjHandle data, ggapiObjHandle *outListOfFutures) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiSubscribeToTopic(
    ggapiSymbol topic,
    ggapiObjHandle callbackHandle,
    ggapiObjHandle *pSubscription) NOEXCEPT;
IMPEXP uint32_t ggapiRegisterPlugin(
    uint32_t moduleHandle, uint32_t componentName, uint32_t callbackHandle) NOEXCEPT;
IMPEXP ggapiErrorKind
ggapiChangeModule(ggapiObjHandle moduleHandleIn, ggapiObjHandle *pPrevHandle) NOEXCEPT;
IMPEXP ggapiErrorKind ggapiRegisterCallback(
    ggapiGenericCallback callbackFunction,
    ggapiContext callbackCtx,
    ggapiSymbol callbackType,
    ggapiObjHandle *pCallbackHandle) NOEXCEPT;
IMPEXP uint32_t ggapiChannelOnClose(uint32_t channel, uint32_t callbackHandle) NOEXCEPT;
IMPEXP uint32_t ggapiChannelListen(uint32_t channel, uint32_t callbackHandle) NOEXCEPT;
IMPEXP uint32_t ggapiChannelWrite(uint32_t channel, uint32_t callStruct) NOEXCEPT;
IMPEXP uint32_t ggapiGetLogLevel(uint64_t *counter, uint32_t cachedLevel) NOEXCEPT;
IMPEXP bool ggapiSetLogLevel(uint32_t level) NOEXCEPT;
IMPEXP bool ggapiLogEvent(uint32_t dataHandle) NOEXCEPT;

#endif // GG_PLUGIN_API
