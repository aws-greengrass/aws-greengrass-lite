// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "embeds_list.h"

#define PATH2(x) #x
#define PATH1(dir, x) PATH2(dir##/src/##x)
#define PATH(dir, x) PATH1(dir, x)
#define EMBED_FILE(file, symbol) \
  .section .rodata; \
  .global symbol; \
  symbol:; \
  .incbin PATH(GGL_COMP_DIR, file); \
  .byte 0;

EMBED_FILE_LIST
