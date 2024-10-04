# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(ggl-config-init LIBS ggl-lib core-bus ggl-file ggl-yaml
                                     core-bus-gg-config ggl-socket)
