# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(ggconfigd LIBS ggl-lib core-bus core-bus-gg-config ggl-json
                               PkgConfig::sqlite3)
target_compile_definitions(ggconfigd
                           PRIVATE "GGL_COMP_DIR=${CMAKE_CURRENT_LIST_DIR}")
target_compile_options(ggconfigd PRIVATE $<$<COMPILE_LANGUAGE:ASM>:-undef>)
