# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(
  ggipcd
  NO_INLINE_TEST
  LIBS gg-sdk
       ggl-common
       ggl-constants
       ggl-policy-validation
       core-bus
       core-bus-gg-config
       core-bus-aws-iot-mqtt
       core-bus-gghealthd
       ggipc-auth
       ggl-socket-server)
