// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_POLICY_VALIDATION_H
#define GGL_POLICY_VALIDATION_H

//! Greengrass accessControl policy resource validation

#include <gg/error.h>
#include <gg/types.h>

/// Returns GG_ERR_OK if @p resource is a valid Greengrass policy resource
/// string, GG_ERR_INVALID otherwise.  A resource is invalid if it contains a
/// bare '?' or a '${...}' escape other than '${*}', '${?}', or '${$}'.
GgError ggl_validate_policy_resource(GgBuffer resource);

#endif
