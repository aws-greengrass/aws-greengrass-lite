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

struct TopicCallbackData {
    uint32_t taskHandle;
    uint32_t topicSymbol;
    uint32_t dataStruct;
};

struct LifecycleCallbackData {
    uint32_t moduleHandle;
    uint32_t phaseSymbol;
    uint32_t dataStruct;
};

struct TaskCallbackData {
    uint32_t dataStruct;
};

typedef uint32_t (*ggapiGenericCallback)(
    uintptr_t callbackContext,
    uint32_t callbackType,
    uint32_t callbackDataSize,
    const void *callbackData) NOEXCEPT;

[[maybe_unused]] EXPORT bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t data) NOEXCEPT;

IMPEXP void ggapiSetError(uint32_t kind, const char *what, size_t len) NOEXCEPT;
IMPEXP uint32_t ggapiGetErrorKind() NOEXCEPT;
IMPEXP const char *ggapiGetErrorWhat() NOEXCEPT;

IMPEXP uint32_t ggapiGetSymbol(const char *bytes, size_t len) NOEXCEPT;
IMPEXP size_t ggapiGetSymbolString(uint32_t symbolInt, char *bytes, size_t len) NOEXCEPT;
IMPEXP size_t ggapiGetSymbolStringLen(uint32_t symbolInt) NOEXCEPT;
IMPEXP uint32_t ggapiCreateStruct() NOEXCEPT;
IMPEXP uint32_t ggapiCreateList() NOEXCEPT;
IMPEXP uint32_t ggapiCreateBuffer() NOEXCEPT;
IMPEXP bool ggapiIsContainer(uint32_t handle) NOEXCEPT;
IMPEXP bool ggapiIsScalar(uint32_t handle) NOEXCEPT;
IMPEXP bool ggapiIsStruct(uint32_t handle) NOEXCEPT;
IMPEXP bool ggapiIsList(uint32_t handle) NOEXCEPT;
IMPEXP bool ggapiIsBuffer(uint32_t handle) NOEXCEPT;
IMPEXP bool ggapiIsTask(uint32_t handle) NOEXCEPT;
IMPEXP bool ggapiIsSubscription(uint32_t handle) NOEXCEPT;
IMPEXP bool ggapiIsScope(uint32_t handle) NOEXCEPT;
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
IMPEXP bool ggapiStructGetBool(uint32_t structHandle, uint32_t keyInt) NOEXCEPT;
IMPEXP uint64_t ggapiStructGetInt64(uint32_t structHandle, uint32_t keyInt) NOEXCEPT;
IMPEXP double ggapiStructGetFloat64(uint32_t structHandle, uint32_t keyInt) NOEXCEPT;
IMPEXP size_t ggapiStructGetStringLen(uint32_t structHandle, uint32_t keyInt) NOEXCEPT;
IMPEXP size_t
ggapiStructGetString(uint32_t structHandle, uint32_t symInt, char *buffer, size_t buflen) NOEXCEPT;
IMPEXP uint32_t ggapiStructGetHandle(uint32_t structHandle, uint32_t keyInt) NOEXCEPT;
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
IMPEXP uint32_t ggapiGetSize(uint32_t structHandle) NOEXCEPT;
IMPEXP bool ggapiStructIsEmpty(uint32_t structHandle) NOEXCEPT;
IMPEXP uint32_t ggapiAnchorHandle(uint32_t anchorHandle, uint32_t objectHandle) NOEXCEPT;
IMPEXP bool ggapiReleaseHandle(uint32_t objectHandle) NOEXCEPT;
IMPEXP uint32_t ggapiToJson(uint32_t containerHandle) NOEXCEPT;
IMPEXP uint32_t ggapiFromJson(uint32_t bufferHandle) NOEXCEPT;
IMPEXP uint32_t ggapiToYaml(uint32_t containerHandle) NOEXCEPT;
IMPEXP uint32_t ggapiFromYaml(uint32_t bufferHandle) NOEXCEPT;
IMPEXP uint32_t ggapiCreateCallScope() NOEXCEPT;
IMPEXP uint32_t ggapiGetCurrentCallScope() NOEXCEPT;
IMPEXP uint32_t ggapiGetCurrentTask() NOEXCEPT;
IMPEXP uint32_t
ggapiSubscribeToTopic(uint32_t anchorHandle, uint32_t topic, uint32_t callbackHandle) NOEXCEPT;
IMPEXP uint32_t ggapiSendToTopic(uint32_t topic, uint32_t callStruct, int32_t timeout) NOEXCEPT;
IMPEXP uint32_t
ggapiSendToListener(uint32_t listenerHandle, uint32_t callStruct, int32_t timeout) NOEXCEPT;
IMPEXP uint32_t ggapiSendToTopicAsync(
    uint32_t topic, uint32_t callStruct, uint32_t callbackHandle, int32_t timeout) NOEXCEPT;
IMPEXP uint32_t ggapiSendToListenerAsync(
    uint32_t listenerHandle,
    uint32_t callStruct,
    uint32_t callbackHandle,
    int32_t timeout) NOEXCEPT;
IMPEXP uint32_t
ggapiCallAsync(uint32_t callStruct, uint32_t callbackHandle, uint32_t delay) NOEXCEPT;
IMPEXP bool ggapiSetSingleThread(bool enable) NOEXCEPT;
IMPEXP uint32_t ggapiWaitForTaskCompleted(uint32_t asyncTask, int32_t timeout) NOEXCEPT;
IMPEXP bool ggapiSleep(uint32_t timeout) NOEXCEPT;
IMPEXP bool ggapiCancelTask(uint32_t asyncTask) NOEXCEPT;
IMPEXP uint32_t ggapiRegisterPlugin(
    uint32_t moduleHandle, uint32_t componentName, uint32_t callbackHandle) NOEXCEPT;
IMPEXP uint32_t ggapiRegisterCallback(
    ggapiGenericCallback callbackFunction, uintptr_t callbackCtx, uint32_t callbackType) NOEXCEPT;

#endif // GG_PLUGIN_API
