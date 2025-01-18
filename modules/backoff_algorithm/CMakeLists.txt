# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(
  backoff_algorithm
  SRCDIR ${backoff_algorithm_SOURCE_DIR}/source
  INCDIRS ${backoff_algorithm_SOURCE_DIR}/source/include)
