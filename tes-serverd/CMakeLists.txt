# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(tes-serverd LIBS ggl-lib ggl-json ggl-http core-bus
                                 PkgConfig::libevent)
