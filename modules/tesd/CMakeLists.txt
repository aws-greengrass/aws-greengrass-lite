# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(tesd LIBS gg-sdk ggl-common ggl-http ggl-proxy-environment
                          core-bus core-bus-gg-config)
