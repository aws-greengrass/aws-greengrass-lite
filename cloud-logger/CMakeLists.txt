# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(cloud-logger LIBS ggipc-client)

set(COMPONENT_NAME "aws.greengrass.CloudLogger")
set(COMPONENT_VERSION "1.0.0")

set(COMPONENTS_DIR "components")
set(LOG_MANAGER_LITE_ARTIFACTS_DIR
    "${COMPONENTS_DIR}/artifacts/${COMPONENT_NAME}/${COMPONENT_VERSION}")
set(RECIPES_DIR "${COMPONENTS_DIR}/recipes")
set(RECIPE_FILE_NAME "${COMPONENT_NAME}-${COMPONENT_VERSION}.yml")

install(TARGETS cloud-logger-bin
        DESTINATION "${LOG_MANAGER_LITE_ARTIFACTS_DIR}")

install(
  FILES recipe.yml
  DESTINATION "${RECIPES_DIR}"
  RENAME "${RECIPE_FILE_NAME}")
