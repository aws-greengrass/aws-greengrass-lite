# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(aws-iot-call LIBS ggl-lib ggl-json core-bus
                                  core-bus-aws-iot-mqtt)
