# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(ggdeploymentd LIBS ggl-lib core-bus ggl-json ggl-yaml
                                   PkgConfig::uuid)
