// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_ERROR_H
#define GGL_IPC_ERROR_H

#include <ggl/attr.h>
#include <ggl/buffer.h>

typedef enum {
    GGL_IPC_ERR_SERVICE_ERROR = 0,
    GGL_IPC_ERR_RESOURCE_NOT_FOUND,
    GGL_IPC_ERR_COMPONENT_NOT_FOUND,
    GGL_IPC_ERR_INVALID_ARGUMENTS,
    GGL_IPC_ERR_UNAUTHORIZED_ERROR,
    GGL_IPC_ERR_CONFLICT_ERROR,
    GGL_IPC_ERR_FAILED_UPDATE_CONDITION_CHECK_ERROR,
    GGL_IPC_ERR_INVALID_TOKEN_ERROR,
    GGL_IPC_ERR_INVALID_RECIPE_DIRECTORY_PATH_ERROR,
    GGL_IPC_ERR_INVALID_ARTIFACTS_DIRECTORY_PATH_ERROR
} GglIpcErrorCode;

typedef struct {
    GglIpcErrorCode error_code;
    GglBuffer message;
} GglIpcError;

static const GglIpcError GGL_IPC_ERROR_DEFAULT UNUSED
= {
      .error_code = GGL_IPC_ERR_SERVICE_ERROR,
  };

void ggl_ipc_err_info(
    GglIpcErrorCode error_code,
    GglBuffer *err_str,
    GglBuffer *service_model_type
) ACCESS(write_only, 2) ACCESS(write_only, 3);

#endif
