# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(
  tesd
  LIBS ggl-lib
       ggl-json
       ggl-http
       core-bus
       core-bus-gg-config
       SOCKETS
       aws_iot_tes)
