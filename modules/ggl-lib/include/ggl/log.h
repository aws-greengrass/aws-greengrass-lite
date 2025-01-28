// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_LOG_H
#define GGL_LOG_H

//! Logging interface

#include <stdint.h>

/// Logging interface implementation.
/// Do not call directly; use one of the macro wrappers.
/// Default implementation prints to stderr.
void ggl_log(
    uint32_t level,
    const char *file,
    int line,
    const char *tag,
    const char *format,
    ...
) __attribute__((format(printf, 5, 6)));

/// No-op logging fn for enabling type checking disabled logging macros.
__attribute__((format(printf, 1, 2), always_inline)) static inline void
ggl_log_disabled(const char *format, ...) {
    (void) format;
}

#define GGL_LOG_NONE 0
#define GGL_LOG_ERROR 1
#define GGL_LOG_WARN 2
#define GGL_LOG_INFO 3
#define GGL_LOG_DEBUG 4
#define GGL_LOG_TRACE 5

/// Minimum log level to print.
/// Can be overridden from make using command line or environment.
#ifndef GGL_LOG_LEVEL
#define GGL_LOG_LEVEL GGL_LOG_INFO
#endif

#ifndef __FILE_NAME__
#define __FILE_NAME__ __FILE__
#endif

#define GGL_LOG(level, ...) \
    ggl_log(level, __FILE_NAME__, __LINE__, GGL_MODULE, __VA_ARGS__)

#if GGL_LOG_LEVEL >= GGL_LOG_ERROR
#define GGL_LOGE(...) GGL_LOG(GGL_LOG_ERROR, __VA_ARGS__)
#else
#define GGL_LOGE(...) ggl_log_disabled(__VA_ARGS__)
#endif

#if GGL_LOG_LEVEL >= GGL_LOG_WARN
#define GGL_LOGW(...) GGL_LOG(GGL_LOG_WARN, __VA_ARGS__)
#else
#define GGL_LOGW(...) ggl_log_disabled(__VA_ARGS__)
#endif

#if GGL_LOG_LEVEL >= GGL_LOG_INFO
#define GGL_LOGI(...) GGL_LOG(GGL_LOG_INFO, __VA_ARGS__)
#else
#define GGL_LOGI(...) ggl_log_disabled(__VA_ARGS__)
#endif

#if GGL_LOG_LEVEL >= GGL_LOG_DEBUG
#define GGL_LOGD(...) GGL_LOG(GGL_LOG_DEBUG, __VA_ARGS__)
#else
#define GGL_LOGD(...) ggl_log_disabled(__VA_ARGS__)
#endif

#if GGL_LOG_LEVEL >= GGL_LOG_TRACE
#define GGL_LOGT(...) GGL_LOG(GGL_LOG_TRACE, __VA_ARGS__)
#else
#define GGL_LOGT(...) ggl_log_disabled(__VA_ARGS__)
#endif

#endif
