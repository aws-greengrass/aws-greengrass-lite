# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(
  aws_sigv4
  SRCDIR ${aws_sigv4_SOURCE_DIR}/source
  INCDIRS config ${aws_sigv4_SOURCE_DIR}/source/include)

# TODO: Remove this line once the upstream Sigv4 library is updated.
target_compile_options(aws_sigv4 PRIVATE "-Wno-conversion")
