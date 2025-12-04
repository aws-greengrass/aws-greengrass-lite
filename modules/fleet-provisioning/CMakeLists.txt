# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

find_package(PkgConfig REQUIRED)
pkg_check_modules(TSS2_ESYS REQUIRED tss2-esys)

ggl_init_module(
  fleet-provision
  LIBS gg-sdk
       ggl-common
       aws-iot-call
       core-bus
       ggl-exec
       ggl-binpath
       core-bus-gg-config
       core-bus-aws-iot-mqtt
       PkgConfig::openssl
       uuid
       ${TSS2_ESYS_LIBRARIES})

target_include_directories(fleet-provision PRIVATE ${TSS2_ESYS_INCLUDE_DIRS})
target_compile_definitions(
  fleet-provision
  PRIVATE "GGL_SYSTEMD_SYSTEM_USER=\"${GGL_SYSTEMD_SYSTEM_USER}\"")
target_compile_definitions(
  fleet-provision
  PRIVATE "GGL_SYSTEMD_SYSTEM_GROUP=\"${GGL_SYSTEMD_SYSTEM_GROUP}\"")
