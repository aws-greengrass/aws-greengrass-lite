// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_AUTH_H
#define GGL_IPC_AUTH_H

//! Greengrass IPC authentication interface
//!
//! This module implements an interface for a GG-IPC server to validate received
//! SVCUID tokens, and a means for components to obtain SVCUID tokens.

#include <sys/types.h>
#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>

/// Authenticate a client and get its component name.
GglError ggl_ipc_auth_lookup_name(
    pid_t pid, GglAlloc *alloc, GglBuffer *component_name
);

#endif
