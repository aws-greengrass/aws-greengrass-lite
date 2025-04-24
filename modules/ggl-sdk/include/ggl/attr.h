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

#endif
