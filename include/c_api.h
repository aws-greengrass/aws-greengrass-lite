#pragma once
#include <cstddef>
#include <cstdint>
#include <ctime>

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

extern "C" [[maybe_unused]] EXPORT bool
    greengrass_lifecycle(uint32_t moduleHandle, uint32_t phase, uint32_t data) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t ggapiClaimThread() noexcept;
extern "C" [[maybe_unused]] IMPEXP bool ggapiReleaseThread() noexcept;
extern "C" [[maybe_unused]] IMPEXP void ggapiSetError(uint32_t errorOrd) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t ggapiGetError() noexcept;

extern "C" [[maybe_unused]] IMPEXP uint32_t
    ggapiGetStringOrdinal(const char *bytes, size_t len) noexcept;
extern "C" [[maybe_unused]] IMPEXP size_t
    ggapiGetOrdinalString(uint32_t ord, char *bytes, size_t len) noexcept;
extern "C" [[maybe_unused]] IMPEXP size_t ggapiGetOrdinalStringLen(uint32_t ord) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t ggapiCreateStruct(uint32_t anchorHandle) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t ggapiCreateList(uint32_t anchorHandle) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t ggapiCreateBuffer(uint32_t anchorHandle) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t
    ggapiCreateBlob(uint32_t anchorHandle, const char *bytes, size_t len) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool
    ggapiStructPutInt64(uint32_t structHandle, uint32_t ord, uint64_t value) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool
    ggapiStructPutFloat64(uint32_t structHandle, uint32_t ord, double value) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool ggapiStructPutString(
    uint32_t structHandle, uint32_t ord, const char *bytes, size_t len
) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool
    ggapiStructPutHandle(uint32_t structHandle, uint32_t ord, uint32_t nestedHandle) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool
    ggapiStructHasKey(uint32_t structHandle, uint32_t ord) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint64_t
    ggapiStructGetInt64(uint32_t structHandle, uint32_t ord) noexcept;
extern "C" [[maybe_unused]] IMPEXP double
    ggapiStructGetFloat64(uint32_t structHandle, uint32_t ord) noexcept;
extern "C" [[maybe_unused]] IMPEXP size_t
    ggapiStructGetStringLen(uint32_t structHandle, uint32_t ord) noexcept;
extern "C" [[maybe_unused]] IMPEXP size_t
    ggapiStructGetString(uint32_t structHandle, uint32_t ord, char *buffer, size_t buflen) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t
    ggapiStructGetHandle(uint32_t structHandle, uint32_t ord) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool
    ggapiListPutInt64(uint32_t listHandle, int32_t idx, uint64_t value) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool
    ggapiListPutFloat64(uint32_t listHandle, int32_t idx, double value) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool
    ggapiListPutString(uint32_t listHandle, int32_t idx, const char *bytes, size_t len) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool
    ggapiListPutHandle(uint32_t listHandle, int32_t idx, uint32_t nestedHandle) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool
    ggapiListInsertInt64(uint32_t listHandle, int32_t idx, uint64_t value) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool
    ggapiListInsertFloat64(uint32_t listHandle, int32_t idx, double value) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool
    ggapiListInsertString(uint32_t listHandle, int32_t idx, const char *bytes, size_t len) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool
    ggapiListInsertHandle(uint32_t listHandle, int32_t idx, uint32_t nestedHandle) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint64_t
    ggapiListGetInt64(uint32_t structHandle, int32_t idx) noexcept;
extern "C" [[maybe_unused]] IMPEXP double
    ggapiListGetFloat64(uint32_t structHandle, int32_t idx) noexcept;
extern "C" [[maybe_unused]] IMPEXP size_t
    ggapiListGetStringLen(uint32_t listHandle, int32_t idx) noexcept;
extern "C" [[maybe_unused]] IMPEXP size_t
    ggapiListGetString(uint32_t structHandle, int32_t idx, char *buffer, size_t buflen) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t
    ggapiListGetHandle(uint32_t listHandle, int32_t idx) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool ggapiBufferPut(
    uint32_t listHandle, int32_t idx, const uint8_t *buffer, uint32_t buflen
) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool ggapiBufferInsert(
    uint32_t listHandle, int32_t idx, const uint8_t *buffer, uint32_t buflen
) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t
    ggapiBufferGet(uint32_t listHandle, int32_t idx, uint8_t *buffer, uint32_t buflen) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool
    ggapiBufferResize(uint32_t structHandle, uint32_t newSize) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t ggapiGetSize(uint32_t structHandle) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t
    ggapiAnchorHandle(uint32_t anchorHandle, uint32_t objectHandle) noexcept;
extern "C" [[maybe_unused]] IMPEXP bool ggapiReleaseHandle(uint32_t objectHandle) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t ggapiGetCurrentTask(void) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t ggapiSubscribeToTopic(
    uint32_t anchorHandle,
    uint32_t topicOrd,
    ggapiTopicCallback rxCallback,
    uintptr_t callbackContext
) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t
    ggapiSendToTopic(uint32_t topicOrd, uint32_t callStruct, int32_t timeout) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t ggapiSendToTopicAsync(
    uint32_t topicOrd,
    uint32_t callStruct,
    ggapiTopicCallback respCallback,
    uintptr_t callbackContext,
    int32_t timeout
) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t ggapiCallNext(uint32_t dataStruct) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t
    ggapiWaitForTaskCompleted(uint32_t asyncTask, int32_t timeout) noexcept;
extern "C" [[maybe_unused]] IMPEXP uint32_t ggapiRegisterPlugin(
    uint32_t moduleHandle,
    uint32_t componentName,
    ggapiLifecycleCallback lifecycleCallback,
    uintptr_t callbackContext
) noexcept;

// Used only by top-level executable
extern "C" [[maybe_unused]] IMPEXP int
    ggapiMainThread(int argc, char *argv[], char *envp[]) noexcept;
