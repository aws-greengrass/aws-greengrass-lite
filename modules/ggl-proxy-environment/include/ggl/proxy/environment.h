// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_PROXY_ENVIRONMENT_H
#define GGL_PROXY_ENVIRONMENT_H

//! Wrapper to set proxy variables from core-bus gg config

#include <ggl/error.h>

// TODO: usage of this module should be replaced by
// setting library proxy settings directly, to avoid runtime
// usage of getenv() with multiple threads running.

/// Set the proxy environment variables used by request libraries.
/// This function requests config values from gg config.
/// This function must be called before other threads are created.
GglError ggl_proxy_set_environment(void);

#endif
