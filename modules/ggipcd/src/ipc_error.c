// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_error.h"
#include <gg/buffer.h>
#include <stddef.h>

void ggl_ipc_err_info(
    GglIpcErrorCode error_code, GgBuffer *err_str, GgBuffer *service_model_type
) {
    GgBuffer err_str_val;
    GgBuffer service_model_type_val;
    switch (error_code) {
    case GGL_IPC_ERR_SERVICE_ERROR:
        err_str_val = GG_STR("ServiceError");
        service_model_type_val = GG_STR("aws.greengrass#ServiceError");
        break;

    case GGL_IPC_ERR_RESOURCE_NOT_FOUND:
        err_str_val = GG_STR("ResourceNotFoundError");
        service_model_type_val = GG_STR("aws.greengrass#ResourceNotFoundError");
        break;

    case GGL_IPC_ERR_INVALID_ARGUMENTS:
        err_str_val = GG_STR("InvalidArgumentsError");
        service_model_type_val = GG_STR("aws.greengrass#InvalidArgumentsError");
        break;

    case GGL_IPC_ERR_COMPONENT_NOT_FOUND:
        err_str_val = GG_STR("ComponentNotFoundError");
        service_model_type_val
            = GG_STR("aws.greengrass#ComponentNotFoundError");
        break;

    case GGL_IPC_ERR_UNAUTHORIZED_ERROR:
        err_str_val = GG_STR("UnauthorizedError");
        service_model_type_val = GG_STR("aws.greengrass#UnauthorizedError");
        break;

    case GGL_IPC_ERR_CONFLICT_ERROR:
        err_str_val = GG_STR("ConflictError");
        service_model_type_val = GG_STR("aws.greengrass#ConflictError");
        break;

    case GGL_IPC_ERR_FAILED_UPDATE_CONDITION_CHECK_ERROR:
        err_str_val = GG_STR("FailedUpdateConditionCheckError");
        service_model_type_val
            = GG_STR("aws.greengrass#FailedUpdateConditionCheckError");
        break;

    case GGL_IPC_ERR_INVALID_TOKEN_ERROR:
        err_str_val = GG_STR("InvalidTokenError");
        service_model_type_val = GG_STR("aws.greengrass#InvalidTokenError");
        break;

    case GGL_IPC_ERR_INVALID_RECIPE_DIRECTORY_PATH_ERROR:
        err_str_val = GG_STR("InvalidRecipeDirectoryPathError");
        service_model_type_val
            = GG_STR("aws.greengrass#InvalidRecipeDirectoryPathError");
        break;

    case GGL_IPC_ERR_INVALID_ARTIFACTS_DIRECTORY_PATH_ERROR:
        err_str_val = GG_STR("InvalidArtifactsDirectoryPathError");
        service_model_type_val
            = GG_STR("aws.greengrass#InvalidArtifactsDirectoryPathError");
        break;
    }

    if (err_str != NULL) {
        *err_str = err_str_val;
    }
    if (service_model_type != NULL) {
        *service_model_type = service_model_type_val;
    }
}
