# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(
  core_mqtt
  SRCDIR coreMQTT/source
  INCDIRS config coreMQTT/source/include coreMQTT/source/interface
  LIBS ggl-lib)

target_compile_definitions(core_mqtt PRIVATE CORE_MQTT_SOURCE)
