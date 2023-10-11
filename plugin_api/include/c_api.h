#ifndef GG_PLUGIN_API
#define GG_PLUGIN_API

#include <stddef.h>
#include <stdint.h>

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
);
typedef bool (*ggapiLifecycleCallback)(
    uintptr_t callbackContext, uint32_t moduleHandle, uint32_t phaseOrd, uint32_t dataStruct
);

EXPORT bool greengrass_lifecycle(uint32_t moduleHandle, uint32_t phase, uint32_t data);
IMPEXP uint32_t ggapiClaimThread();
IMPEXP bool ggapiReleaseThread();
IMPEXP void ggapiSetError(uint32_t errorOrd);
IMPEXP uint32_t ggapiGetError();

IMPEXP uint32_t ggapiGetStringOrdinal(const char *bytes, size_t len);
IMPEXP size_t ggapiGetOrdinalString(uint32_t ord, char *bytes, size_t len);
IMPEXP size_t ggapiGetOrdinalStringLen(uint32_t ord);
IMPEXP uint32_t ggapiCreateStruct(uint32_t anchorHandle);
IMPEXP uint32_t ggapiCreateList(uint32_t anchorHandle);
IMPEXP uint32_t ggapiCreateBuffer(uint32_t anchorHandle);
IMPEXP uint32_t ggapiCreateBlob(uint32_t anchorHandle, const char *bytes, size_t len);
IMPEXP bool ggapiStructPutInt64(uint32_t structHandle, uint32_t ord, uint64_t value);
IMPEXP bool ggapiStructPutFloat64(uint32_t structHandle, uint32_t ord, double value);
IMPEXP bool ggapiStructPutString(
    uint32_t structHandle, uint32_t ord, const char *bytes, size_t len
);
IMPEXP bool ggapiStructPutHandle(uint32_t structHandle, uint32_t ord, uint32_t nestedHandle);
IMPEXP bool ggapiStructHasKey(uint32_t structHandle, uint32_t ord);
IMPEXP uint64_t ggapiStructGetInt64(uint32_t structHandle, uint32_t ord);
IMPEXP double ggapiStructGetFloat64(uint32_t structHandle, uint32_t ord);
IMPEXP size_t ggapiStructGetStringLen(uint32_t structHandle, uint32_t ord);
IMPEXP size_t
ggapiStructGetString(uint32_t structHandle, uint32_t ord, char *buffer, size_t buflen);
IMPEXP uint32_t ggapiStructGetHandle(uint32_t structHandle, uint32_t ord);
IMPEXP bool ggapiListPutInt64(uint32_t listHandle, int32_t idx, uint64_t value);
IMPEXP bool ggapiListPutFloat64(uint32_t listHandle, int32_t idx, double value);
IMPEXP bool ggapiListPutString(uint32_t listHandle, int32_t idx, const char *bytes, size_t len);
IMPEXP bool ggapiListPutHandle(uint32_t listHandle, int32_t idx, uint32_t nestedHandle);
IMPEXP bool ggapiListInsertInt64(uint32_t listHandle, int32_t idx, uint64_t value);
IMPEXP bool ggapiListInsertFloat64(uint32_t listHandle, int32_t idx, double value);
IMPEXP bool ggapiListInsertString(uint32_t listHandle, int32_t idx, const char *bytes, size_t len);
IMPEXP bool ggapiListInsertHandle(uint32_t listHandle, int32_t idx, uint32_t nestedHandle);
IMPEXP uint64_t ggapiListGetInt64(uint32_t structHandle, int32_t idx);
IMPEXP double ggapiListGetFloat64(uint32_t structHandle, int32_t idx);
IMPEXP size_t ggapiListGetStringLen(uint32_t listHandle, int32_t idx);
IMPEXP size_t ggapiListGetString(uint32_t structHandle, int32_t idx, char *buffer, size_t buflen);
IMPEXP uint32_t ggapiListGetHandle(uint32_t listHandle, int32_t idx);
IMPEXP bool ggapiBufferPut(
    uint32_t listHandle, int32_t idx, const uint8_t *buffer, uint32_t buflen
);
IMPEXP bool ggapiBufferInsert(
    uint32_t listHandle, int32_t idx, const uint8_t *buffer, uint32_t buflen
);
IMPEXP uint32_t ggapiBufferGet(uint32_t listHandle, int32_t idx, uint8_t *buffer, uint32_t buflen);
IMPEXP bool ggapiBufferResize(uint32_t structHandle, uint32_t newSize);
IMPEXP uint32_t ggapiGetSize(uint32_t structHandle);
IMPEXP uint32_t ggapiAnchorHandle(uint32_t anchorHandle, uint32_t objectHandle);
IMPEXP bool ggapiReleaseHandle(uint32_t objectHandle);
IMPEXP uint32_t ggapiGetCurrentTask(void);
IMPEXP uint32_t ggapiSubscribeToTopic(
    uint32_t anchorHandle,
    uint32_t topicOrd,
    ggapiTopicCallback rxCallback,
    uintptr_t callbackContext
);
IMPEXP uint32_t ggapiSendToTopic(uint32_t topicOrd, uint32_t callStruct, int32_t timeout);
IMPEXP uint32_t ggapiSendToTopicAsync(
    uint32_t topicOrd,
    uint32_t callStruct,
    ggapiTopicCallback respCallback,
    uintptr_t callbackContext,
    int32_t timeout
);
IMPEXP uint32_t ggapiCallNext(uint32_t dataStruct);
IMPEXP uint32_t ggapiWaitForTaskCompleted(uint32_t asyncTask, int32_t timeout);
IMPEXP uint32_t ggapiRegisterPlugin(
    uint32_t moduleHandle,
    uint32_t componentName,
    ggapiLifecycleCallback lifecycleCallback,
    uintptr_t callbackContext
);

#endif
