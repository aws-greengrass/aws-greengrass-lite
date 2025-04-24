// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <ggl/buffer.h>
#include <ggl/ipc/error.h>
#include <ggl/log.h>
#include <stddef.h>

void ggl_ipc_err_info(
    GglIpcErrorCode error_code,
    GglBuffer *err_str,
    GglBuffer *service_model_type
) {
    GglBuffer err_str_val;
    GglBuffer service_model_type_val;
    switch (error_code) {
    case GGL_IPC_ERR_SERVICE_ERROR:
        err_str_val = GGL_STR("ServiceError");
        service_model_type_val = GGL_STR("aws.greengrass#ServiceError");
        break;

    case GGL_IPC_ERR_RESOURCE_NOT_FOUND:
        err_str_val = GGL_STR("ResourceNotFoundError");
        service_model_type_val
            = GGL_STR("aws.greengrass#ResourceNotFoundError");
        break;

    case GGL_IPC_ERR_INVALID_ARGUMENTS:
        err_str_val = GGL_STR("InvalidArgumentsError");
        service_model_type_val
            = GGL_STR("aws.greengrass#InvalidArgumentsError");
        break;

    case GGL_IPC_ERR_COMPONENT_NOT_FOUND:
        err_str_val = GGL_STR("ComponentNotFoundError");
        service_model_type_val
            = GGL_STR("aws.greengrass#ComponentNotFoundError");
        break;

    case GGL_IPC_ERR_UNAUTHORIZED_ERROR:
        err_str_val = GGL_STR("UnauthorizedError");
        service_model_type_val = GGL_STR("aws.greengrass#UnauthorizedError");
        break;

    case GGL_IPC_ERR_CONFLICT_ERROR:
        err_str_val = GGL_STR("ConflictError");
        service_model_type_val = GGL_STR("aws.greengrass#ConflictError");
        break;

    case GGL_IPC_ERR_FAILED_UPDATE_CONDITION_CHECK_ERROR:
        err_str_val = GGL_STR("FailedUpdateConditionCheckError");
        service_model_type_val
            = GGL_STR("aws.greengrass#FailedUpdateConditionCheckError");
        break;

    case GGL_IPC_ERR_INVALID_TOKEN_ERROR:
        err_str_val = GGL_STR("InvalidTokenError");
        service_model_type_val = GGL_STR("aws.greengrass#InvalidTokenError");
        break;

    case GGL_IPC_ERR_INVALID_RECIPE_DIRECTORY_PATH_ERROR:
        err_str_val = GGL_STR("InvalidRecipeDirectoryPathError");
        service_model_type_val
            = GGL_STR("aws.greengrass#InvalidRecipeDirectoryPathError");
        break;

    case GGL_IPC_ERR_INVALID_ARTIFACTS_DIRECTORY_PATH_ERROR:
        err_str_val = GGL_STR("InvalidArtifactsDirectoryPathError");
        service_model_type_val
            = GGL_STR("aws.greengrass#InvalidArtifactsDirectoryPathError");
        break;
    }

    if (err_str != NULL) {
        *err_str = err_str_val;
    }
    if (service_model_type != NULL) {
        *service_model_type = service_model_type_val;
    }
}

GglIpcErrorCode get_ipc_err_info(GglBuffer error_code) {
    GglIpcErrorCode ret;
    if (ggl_buffer_eq(GGL_STR("ServiceError"), error_code)) {
        ret = GGL_IPC_ERR_SERVICE_ERROR;
    } else if (ggl_buffer_eq(GGL_STR("ResourceNotFoundError"), error_code)) {
        ret = GGL_IPC_ERR_RESOURCE_NOT_FOUND;
    } else if (ggl_buffer_eq(GGL_STR("InvalidArgumentsError"), error_code)) {
        ret = GGL_IPC_ERR_INVALID_ARGUMENTS;
    } else if (ggl_buffer_eq(GGL_STR("ComponentNotFoundError"), error_code)) {
        ret = GGL_IPC_ERR_COMPONENT_NOT_FOUND;
    } else if (ggl_buffer_eq(GGL_STR("UnauthorizedError"), error_code)) {
        ret = GGL_IPC_ERR_UNAUTHORIZED_ERROR;
    } else if (ggl_buffer_eq(GGL_STR("ConflictError"), error_code)) {
        ret = GGL_IPC_ERR_CONFLICT_ERROR;
    } else if (ggl_buffer_eq(
                   GGL_STR("FailedUpdateConditionCheckError"), error_code
               )) {
        ret = GGL_IPC_ERR_FAILED_UPDATE_CONDITION_CHECK_ERROR;
    } else if (ggl_buffer_eq(GGL_STR("InvalidTokenError"), error_code)) {
        ret = GGL_IPC_ERR_INVALID_TOKEN_ERROR;
    } else if (ggl_buffer_eq(
                   GGL_STR("InvalidRecipeDirectoryPathError"), error_code
               )) {
        ret = GGL_IPC_ERR_INVALID_RECIPE_DIRECTORY_PATH_ERROR;
    } else if (ggl_buffer_eq(
                   GGL_STR("InvalidArtifactsDirectoryPathError"), error_code
               )) {
        ret = GGL_IPC_ERR_INVALID_ARTIFACTS_DIRECTORY_PATH_ERROR;
    } else {
        GGL_LOGW("Unknown error code.");
        ret = GGL_IPC_ERR_SERVICE_ERROR;
    }

    return ret;
}
