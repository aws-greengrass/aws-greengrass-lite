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
#include <ggl/buffer.h>
#include <ggl/error.h>

/// Authenticate a client by checking if its pid is associated with its claimed
/// component name.
GglError ggl_ipc_auth_validate_name(pid_t pid, GglBuffer component_name);

#endif
