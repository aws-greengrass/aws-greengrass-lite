# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(
  fleet-provision
  LIBS ggl-sdk
       ggl-common
       core-bus
       ggl-exec
       core-bus-gg-config
       core-bus-aws-iot-mqtt
       PkgConfig::openssl)
target_compile_definitions(
  fleet-provision
  PRIVATE "GGL_SYSTEMD_SYSTEM_USER=\"${GGL_SYSTEMD_SYSTEM_USER}\"")
target_compile_definitions(
  fleet-provision
  PRIVATE "GGL_SYSTEMD_SYSTEM_GROUP=\"${GGL_SYSTEMD_SYSTEM_GROUP}\"")
