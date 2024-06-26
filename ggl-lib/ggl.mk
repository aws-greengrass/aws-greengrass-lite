# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

GGL_LOG_LEVEL ?= INFO
CPPFLAGS += -DGGL_LOG_LEVEL=GGL_LOG_$(GGL_LOG_LEVEL)
