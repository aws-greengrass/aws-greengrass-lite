# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(ggl-sdk INCDIRS include priv_include)

block()
string(TOUPPER "${GGL_LOG_LEVEL}" level)
set(default "$<IF:$<CONFIG:Debug>,DEBUG,WARN>")
set(choose_level "$<IF:$<BOOL:${level}>,${level},${default}>")
target_compile_definitions(ggl-sdk PUBLIC GGL_LOG_LEVEL=GGL_LOG_${choose_level})
endblock()
