# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(hello-world LIBS ggl-lib ggipc-client)

set(COMPONENT_NAME "com.example.LiteHelloWorld")
set(COMPONENT_VERSION "0.1.0")

set(COMPONENTS_DIR "components")
set(ARTIFACTS_DIR
    "${COMPONENTS_DIR}/artifacts/${COMPONENT_NAME}/${COMPONENT_VERSION}")
set(RECIPES_DIR "${COMPONENTS_DIR}/recipes")
set(RECIPE_FILE_NAME "${COMPONENT_NAME}-${COMPONENT_VERSION}.yaml")

install(TARGETS hello-world-bin DESTINATION "${ARTIFACTS_DIR}")

install(
  FILES recipe.yml
  DESTINATION "${RECIPES_DIR}"
  RENAME "${RECIPE_FILE_NAME}")
