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

typedef uint32_t (*ggapiTopicCallback)(
    uintptr_t callbackContext, uint32_t taskHandle, uint32_t topicOrd, uint32_t dataStruct
) NOEXCEPT;
typedef bool (*ggapiLifecycleCallback)(
    uintptr_t callbackContext, uint32_t moduleHandle, uint32_t phaseOrd, uint32_t dataStruct
) NOEXCEPT;

EXPORT bool greengrass_lifecycle(uint32_t moduleHandle, uint32_t phase, uint32_t data) NOEXCEPT;
IMPEXP uint32_t ggapiClaimThread() NOEXCEPT;
IMPEXP bool ggapiReleaseThread() NOEXCEPT;
IMPEXP void ggapiSetError(uint32_t errorOrd) NOEXCEPT;
IMPEXP uint32_t ggapiGetError() NOEXCEPT;

IMPEXP uint32_t ggapiGetStringOrdinal(const char *bytes, size_t len) NOEXCEPT;
IMPEXP size_t ggapiGetOrdinalString(uint32_t ord, char *bytes, size_t len) NOEXCEPT;
IMPEXP size_t ggapiGetOrdinalStringLen(uint32_t ord) NOEXCEPT;
IMPEXP uint32_t ggapiCreateStruct(uint32_t anchorHandle) NOEXCEPT;
IMPEXP uint32_t ggapiCreateList(uint32_t anchorHandle) NOEXCEPT;
IMPEXP uint32_t ggapiCreateBuffer(uint32_t anchorHandle) NOEXCEPT;
IMPEXP bool ggapiIsStruct(uint32_t handle) NOEXCEPT;
IMPEXP bool ggapiIsList(uint32_t handle) NOEXCEPT;
IMPEXP bool ggapiIsBuffer(uint32_t handle) NOEXCEPT;
IMPEXP bool ggapiIsSubscription(uint32_t handle) NOEXCEPT;
IMPEXP bool ggapiIsScope(uint32_t handle) NOEXCEPT;
IMPEXP bool ggapiIsSameObject(uint32_t handle1, uint32_t handle2) NOEXCEPT;
IMPEXP bool ggapiStructPutBool(uint32_t structHandle, uint32_t ord, bool value) NOEXCEPT;
IMPEXP bool ggapiStructPutInt64(uint32_t structHandle, uint32_t ord, uint64_t value) NOEXCEPT;
IMPEXP bool ggapiStructPutFloat64(uint32_t structHandle, uint32_t ord, double value) NOEXCEPT;
IMPEXP bool ggapiStructPutString(uint32_t structHandle, uint32_t ord, const char *bytes, size_t len)
    NOEXCEPT;
IMPEXP bool ggapiStructPutStringOrd(uint32_t listHandle, uint32_t ord, uint32_t stringOrd) NOEXCEPT;
IMPEXP bool ggapiStructPutHandle(uint32_t structHandle, uint32_t ord, uint32_t nestedHandle)
    NOEXCEPT;
IMPEXP bool ggapiStructHasKey(uint32_t structHandle, uint32_t ord) NOEXCEPT;
IMPEXP bool ggapiStructGetBool(uint32_t structHandle, uint32_t ord) NOEXCEPT;
IMPEXP uint64_t ggapiStructGetInt64(uint32_t structHandle, uint32_t ord) NOEXCEPT;
IMPEXP double ggapiStructGetFloat64(uint32_t structHandle, uint32_t ord) NOEXCEPT;
IMPEXP size_t ggapiStructGetStringLen(uint32_t structHandle, uint32_t ord) NOEXCEPT;
IMPEXP size_t
ggapiStructGetString(uint32_t structHandle, uint32_t ord, char *buffer, size_t buflen) NOEXCEPT;
IMPEXP uint32_t ggapiStructGetHandle(uint32_t structHandle, uint32_t ord) NOEXCEPT;
IMPEXP bool ggapiListPutBool(uint32_t listHandle, int32_t idx, bool value) NOEXCEPT;
IMPEXP bool ggapiListPutInt64(uint32_t listHandle, int32_t idx, uint64_t value) NOEXCEPT;
IMPEXP bool ggapiListPutFloat64(uint32_t listHandle, int32_t idx, double value) NOEXCEPT;
IMPEXP bool ggapiListPutString(uint32_t listHandle, int32_t idx, const char *bytes, size_t len)
    NOEXCEPT;
IMPEXP bool ggapiListPutStringOrd(uint32_t listHandle, int32_t idx, uint32_t stringOrd) NOEXCEPT;
IMPEXP bool ggapiListPutHandle(uint32_t listHandle, int32_t idx, uint32_t nestedHandle) NOEXCEPT;
IMPEXP bool ggapiListInsertBool(uint32_t listHandle, int32_t idx, bool value) NOEXCEPT;
IMPEXP bool ggapiListInsertInt64(uint32_t listHandle, int32_t idx, uint64_t value) NOEXCEPT;
IMPEXP bool ggapiListInsertFloat64(uint32_t listHandle, int32_t idx, double value) NOEXCEPT;
IMPEXP bool ggapiListInsertString(uint32_t listHandle, int32_t idx, const char *bytes, size_t len)
    NOEXCEPT;
IMPEXP bool ggapiListInsertStringOrd(uint32_t listHandle, int32_t idx, uint32_t stringOrd) NOEXCEPT;
IMPEXP bool ggapiListInsertHandle(uint32_t listHandle, int32_t idx, uint32_t nestedHandle) NOEXCEPT;
IMPEXP bool ggapiListGetBool(uint32_t structHandle, int32_t idx) NOEXCEPT;
IMPEXP uint64_t ggapiListGetInt64(uint32_t structHandle, int32_t idx) NOEXCEPT;
IMPEXP double ggapiListGetFloat64(uint32_t structHandle, int32_t idx) NOEXCEPT;
IMPEXP size_t ggapiListGetStringLen(uint32_t listHandle, int32_t idx) NOEXCEPT;
IMPEXP size_t
ggapiListGetString(uint32_t structHandle, int32_t idx, char *buffer, size_t buflen) NOEXCEPT;
IMPEXP uint32_t ggapiListGetHandle(uint32_t listHandle, int32_t idx) NOEXCEPT;
IMPEXP bool ggapiBufferPut(uint32_t listHandle, int32_t idx, const char *buffer, uint32_t buflen)
    NOEXCEPT;
IMPEXP bool ggapiBufferInsert(uint32_t listHandle, int32_t idx, const char *buffer, uint32_t buflen)
    NOEXCEPT;
IMPEXP uint32_t
ggapiBufferGet(uint32_t listHandle, int32_t idx, char *buffer, uint32_t buflen) NOEXCEPT;
IMPEXP bool ggapiBufferResize(uint32_t structHandle, uint32_t newSize) NOEXCEPT;
IMPEXP uint32_t ggapiGetSize(uint32_t structHandle) NOEXCEPT;
IMPEXP uint32_t ggapiAnchorHandle(uint32_t anchorHandle, uint32_t objectHandle) NOEXCEPT;
IMPEXP bool ggapiReleaseHandle(uint32_t objectHandle) NOEXCEPT;
IMPEXP uint32_t ggapiGetCurrentTask() NOEXCEPT;
IMPEXP uint32_t ggapiSubscribeToTopic(
    uint32_t anchorHandle,
    uint32_t topicOrd,
    ggapiTopicCallback rxCallback,
    uintptr_t callbackContext
) NOEXCEPT;
IMPEXP uint32_t ggapiSendToTopic(uint32_t topicOrd, uint32_t callStruct, int32_t timeout) NOEXCEPT;
IMPEXP uint32_t
ggapiSendToListener(uint32_t listenerHandle, uint32_t callStruct, int32_t timeout) NOEXCEPT;
IMPEXP uint32_t ggapiSendToTopicAsync(
    uint32_t topicOrd,
    uint32_t callStruct,
    ggapiTopicCallback respCallback,
    uintptr_t callbackContext,
    int32_t timeout
) NOEXCEPT;
IMPEXP uint32_t ggapiSendToListenerAsync(
    uint32_t listenerHandle,
    uint32_t callStruct,
    ggapiTopicCallback respCallback,
    uintptr_t callbackContext,
    int32_t timeout
) NOEXCEPT;
IMPEXP uint32_t ggapiCallNext(uint32_t dataStruct) NOEXCEPT;
IMPEXP uint32_t ggapiWaitForTaskCompleted(uint32_t asyncTask, int32_t timeout) NOEXCEPT;
IMPEXP bool ggapiCancelTask(uint32_t asyncTask) NOEXCEPT;
IMPEXP uint32_t ggapiRegisterPlugin(
    uint32_t moduleHandle,
    uint32_t componentName,
    ggapiLifecycleCallback lifecycleCallback,
    uintptr_t callbackContext
) NOEXCEPT;

#endif // GG_PLUGIN_API
