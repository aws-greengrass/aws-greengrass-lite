# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(
  core_mqtt
  SRCDIR ${core_mqtt_SOURCE_DIR}/source
  INCDIRS config ${core_mqtt_SOURCE_DIR}/source/include
          ${core_mqtt_SOURCE_DIR}/source/interface
  LIBS ggl-sdk)

target_compile_definitions(core_mqtt PRIVATE CORE_MQTT_SOURCE)
