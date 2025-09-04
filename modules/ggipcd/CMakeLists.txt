# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(
  ggipcd
  LIBS ggl-sdk
       ggl-common
       ggl-constants
       core-bus
       core-bus-gg-config
       core-bus-aws-iot-mqtt
       core-bus-gghealthd
       ggipc-auth
       ggl-rand
       ggl-socket-server)
