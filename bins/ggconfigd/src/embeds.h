// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGCONFIGD_EMBEDS_H
#define GGCONFIGD_EMBEDS_H

#include "embeds_list.h"

#define EMBED_FILE(file, symbol) extern const char symbol[];

EMBED_FILE_LIST

#endif
