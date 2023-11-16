#ifndef NUCLEUS_CORE_H
#define NUCLEUS_CORE_H

#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
#define NOEXCEPT noexcept
#else
#define NOEXCEPT
#endif

EXPORT int ggapiMainThread(int argc, char *argv[], char *envp[]) NOEXCEPT;

#endif
