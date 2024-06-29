/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_ERROR_H
#define GGL_ERROR_H

/*! GGL error codes */

/** GGL error codes, representing class of error. */
typedef enum GglError {
    /** Success */
    GGL_ERR_OK = 0,
    /** Generic failure */
    GGL_ERR_FAILURE,
    /** Failure, can be retried */
    GGL_ERR_RETRY,
    /** Request cannot be handled at the time */
    GGL_ERR_BUSY,
    /** System is in irrecoverably broken state */
    GGL_ERR_FATAL,
    /** Request is invalid or malformed */
    GGL_ERR_INVALID,
    /** Request is unsupported */
    GGL_ERR_UNSUPPORTED,
    /** Request data invalid */
    GGL_ERR_PARSE,
    /** Request or data outside of allowable range */
    GGL_ERR_RANGE,
    /** Insufficient memory */
    GGL_ERR_NOMEM,
    /** No connection */
    GGL_ERR_NOCONN,
    /** Unknown entry or target requested */
    GGL_ERR_NOENTRY,
    /** Invalid or missing configuration */
    GGL_ERR_CONFIG,
} GglError;

#endif
