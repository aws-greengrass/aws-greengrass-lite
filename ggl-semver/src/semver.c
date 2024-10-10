// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/semver.h"
#include <sys/types.h>
#include <ctype.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_VERSION_LEN 512
const char SEMVER[] = "semver";

static bool process_version(
    GglByteVec current_requirement, char *current_version
) {
    bool return_status = false;

    if (current_requirement.buf.data[0] == '>') {
        if (current_requirement.buf.data[1] == '=') {
            if (strverscmp(
                    current_version, (char *) &current_requirement.buf.data[2]
                )
                >= 0) {
                return_status = true;
            }

        } else if (strverscmp(
                       current_version,
                       (char *) &current_requirement.buf.data[1]
                   )
                   > 0) {
            return_status = true;
        }

    } else if (current_requirement.buf.data[0] == '<') {
        if (current_requirement.buf.data[1] == '=') {
            if (strverscmp(
                    current_version, (char *) &current_requirement.buf.data[2]
                )
                <= 0) {
                return_status = true;
            }

        } else if (strverscmp(
                       current_version,
                       (char *) &current_requirement.buf.data[1]
                   )
                   < 0) {
            return_status = true;
        }
    } else if (((current_requirement.buf.data[0] == '=')
                && (strverscmp(
                        current_version,
                        (char *) &current_requirement.buf.data[1]
                    )
                    == 0))
               || ((isdigit(current_requirement.buf.data[0]))
                   && (strverscmp(
                           current_version,
                           (char *) &current_requirement.buf.data[0]
                       )
                       == 0))) {
        return_status = true;
    } else {
        return_status = false;
    }
    return return_status;
}

bool is_in_range(GglBuffer version, GglBuffer requirements_range) {
    char *requirements_range_as_char = (char *) requirements_range.data;

    static uint8_t work_mem_buffer[MAX_VERSION_LEN];
    GglByteVec work_mem_vec = GGL_BYTE_VEC(work_mem_buffer);

    static uint8_t current_version_buffer[MAX_VERSION_LEN];
    GglByteVec current_version_vec = GGL_BYTE_VEC(current_version_buffer);
    GglError ret = ggl_byte_vec_append(&current_version_vec, version);
    ggl_byte_vec_chain_append(&ret, &current_version_vec, GGL_STR("\0"));
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(SEMVER, "Failed to copy information over");
        return ret;
    }

    for (ulong index = 0; index < requirements_range.len; index++) {
        if (requirements_range_as_char[index] == ' ') {
            // null terminating as strverscmp requires it
            ret = ggl_byte_vec_append(&work_mem_vec, GGL_STR("\0"));
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to copy information over");
                return ret;
            }
            bool result = process_version(
                work_mem_vec, (char *) current_version_vec.buf.data
            );
            if (result == false) {
                GGL_LOGT("Requirement wasn't satisfied");
                return false;
            }
            // Rest once a value is parsed
            work_mem_vec.buf.len = 0;
            index++;
        }
        ret = ggl_byte_vec_append(
            &work_mem_vec,
            (GglBuffer) { (uint8_t *) &requirements_range_as_char[index], 1 }
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to copy information over");
            return false;
        }
    }

    if (work_mem_vec.buf.len != 0) {
        ret = ggl_byte_vec_append(&work_mem_vec, GGL_STR("\0"));
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(SEMVER, "Failed to copy information over");
            return ret;
        }
        bool result = process_version(
            work_mem_vec, (char *) current_version_vec.buf.data
        );
        if (result == false) {
            GGL_LOGT(SEMVER, "Requirement wasn't satisfied");
            return false;
        }
    }

    return true;
}
