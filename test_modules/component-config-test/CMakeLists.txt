# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

# Unit test for extract_merge_payload() in component_config.c. Compiles
# component_config.c directly so no live IPC is needed.
ggl_init_module(component-config-test LIBS gg-sdk ggl-common ggl-json core-bus
                                           core-bus-gg-config)

# The bin/component-config-test.c needs the component_config.c sources and its
# header. Add them to the generated test binary target.
target_sources(
  component-config-test-bin
  PRIVATE ${CMAKE_SOURCE_DIR}/modules/ggdeploymentd/src/component_config.c)
target_include_directories(
  component-config-test-bin
  PRIVATE ${CMAKE_SOURCE_DIR}/modules/ggdeploymentd/src)
