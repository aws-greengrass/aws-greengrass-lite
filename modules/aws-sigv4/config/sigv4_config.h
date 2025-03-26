/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AWS_SIGV4_CONFIG_H
#define AWS_SIGV4_CONFIG_H

// We need a bigger buffer as Security token is included in the signature
// calculation.
#define SIGV4_PROCESSING_BUFFER_LENGTH 4096U

#endif
