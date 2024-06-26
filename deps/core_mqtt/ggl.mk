# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

deps/core_mqtt_INCDIRS := config \
		coreMQTT/source/include \
		coreMQTT/source/interface
deps/core_mqtt_SRCDIR := coreMQTT/source
deps/core_mqtt_CPPFLAGS := -DCORE_MQTT_SOURCE
deps/core_mqtt_LIBS := ggl-lib
