// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_CONFIG_INTERPOLATION_H
#define GGL_CONFIG_INTERPOLATION_H

#include <config_reader.h>
#include <gg/error.h>
#include <gg/io.h>
#include <gg/types.h>

/// Writes the value of a Greengrass component recipe variable
/// "<namespace>:<key>" into a GgWriter.
///
/// Although recipes uses curly braces {} to denote an escape sequence,
/// braces must be stripped before calling this function.
///
/// For configuration variables i.e. "configuration:<json_ptr>" a config
/// reader callback is required. Pass GGL_CONFIG_NULL_READER to return an error
/// if config lookup is not desired.
///
/// An error is returned if the escape sequence does not contain a colon or the
/// namespace-key pair is not recognized.
GgError ggl_substitute_escape(
    GgWriter writer,
    GgBuffer recipe_variable,
    GgBuffer root_path,
    GgBuffer component_name,
    GgBuffer component_version,
    GgBuffer thing_name,
    GgConfigReader config_reader
);

#endif
