# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(
  recipe2unit
  LIBS ggl-lib
       ggl-json
       ggl-yaml
       ggl-file
       ggl-recipe
       core-bus
       ggl-constants
       core-bus-gg-config)
