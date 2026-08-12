// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_REGEX_H
#define GGL_REGEX_H

//! Allocation-free Thompson NFA regex engine.

#include <gg/arena.h>
#include <gg/error.h>
#include <gg/types.h>
#include <stdbool.h>

/// Minimum arena size (bytes) required by ggl_regex_match.
/// Callers may declare a stack buffer of at least this size and initialise
/// an arena over it with gg_arena_init.
///
/// Breakdown (all from GGL_REGEX_MAX_PROG = 512):
///   NfaInst prog[512] * 12 bytes         = 6144
///   4 x uint16_t[512] simulation arrays  = 4096
///   Alignment padding overhead            < 128
///   Total (rounded up)                    = 10368
#define GGL_REGEX_MIN_ARENA_SIZE 10368

/// Match a bare regex pattern against a subject using a Thompson NFA.
///
/// @param pattern  The regex pattern (NO surrounding slashes).
/// @param subject  The string to match against.
/// @param arena    Caller-provided arena for working memory.  Must have at
///                 least GGL_REGEX_MIN_ARENA_SIZE bytes available.
/// @param matches  Out-parameter set to true/false on GG_ERR_OK.
///
/// @return GG_ERR_OK      Pattern is valid; *matches is set.
/// @return GG_ERR_INVALID Malformed pattern (unbalanced paren, unterminated
///                        bracket, trailing backslash, quantifier with nothing
///                        to repeat).
/// @return GG_ERR_RANGE   Pattern exceeds length cap or nesting cap.
/// @return GG_ERR_NOMEM   Arena cannot supply the working memory.
GgError ggl_regex_match(
    GgBuffer pattern, GgBuffer subject, GgArena *arena, bool *matches
);

#endif
