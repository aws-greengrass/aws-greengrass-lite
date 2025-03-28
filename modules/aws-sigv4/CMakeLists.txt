# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(
  aws_sigv4
  SRCDIR ${aws_sigv4_SOURCE_DIR}/source
  INCDIRS config ${aws_sigv4_SOURCE_DIR}/source/include)
