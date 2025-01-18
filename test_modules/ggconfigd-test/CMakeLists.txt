# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(ggconfigd-test LIBS ggl-lib core-bus core-bus-gg-config
                                    ggl-json ggconfigd PkgConfig::sqlite3)
