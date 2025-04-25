// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_ATTR_H
#define GGL_ATTR_H

#ifdef __has_c_attribute
#if __has_c_attribute(nodiscard)
#define NODISCARD [[nodiscard]]
#endif
#endif

#ifndef NODISCARD
#define NODISCARD
#endif

#ifdef __has_attribute
#if __has_attribute(counted_by)
#define COUNTED_BY(field) __attribute__((counted_by(field)))
#endif
#endif

#ifndef COUNTED_BY
#define COUNTED_BY(field)
#endif

#ifdef __has_attribute
#if __has_attribute(designated_init)
#define DESIGNATED_INIT __attribute__((designated_init))
#endif
#endif

#ifndef DESIGNATED_INIT
#define DESIGNATED_INIT
#endif

#ifdef __has_attribute
#if __has_attribute(alloc_size)
#define ALLOC_SIZE(...) __attribute__((alloc_size(__VA_ARGS__)))
#endif
#endif

#ifndef ALLOC_SIZE
#define ALLOC_SIZE(...)
#endif

#ifdef __has_attribute
#if __has_attribute(alloc_align)
#define ALLOC_ALIGN(pos) __attribute__((alloc_align(pos)))
#endif
#endif

#ifndef ALLOC_ALIGN
#define ALLOC_ALIGN(pos)
#endif

#ifdef __has_attribute
#if __has_attribute(nonnull)
#define NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#endif
#endif

#ifndef NONNULL
#define NONNULL(...)
#endif

#ifdef __has_attribute
#if __has_attribute(null_terminated_string_arg)
#define NULL_TERMINATED_STRING_ARG(pos) \
    __attribute__((null_terminated_string_arg(pos)))
#endif
#endif

#ifndef NULL_TERMINATED_STRING_ARG
#define NULL_TERMINATED_STRING_ARG(pos)
#endif

#ifdef __has_attribute
#if __has_attribute(cold)
#define COLD __attribute__((cold))
#endif
#endif

#ifndef COLD
#define COLD
#endif

#endif
