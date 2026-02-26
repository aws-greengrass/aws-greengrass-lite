// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <ctype.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/semver.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

static bool process_version(
    GgByteVec current_requirement, char *current_version
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

bool is_in_range(GgBuffer version, GgBuffer requirements_range) {
    char *requirements_range_as_char = (char *) requirements_range.data;

    static uint8_t work_mem_buffer[NAME_MAX];
    GgByteVec work_mem_vec = GG_BYTE_VEC(work_mem_buffer);

    static uint8_t current_version_buffer[NAME_MAX];
    GgByteVec current_version_vec = GG_BYTE_VEC(current_version_buffer);
    GgError ret = gg_byte_vec_append(&current_version_vec, version);
    gg_byte_vec_chain_append(&ret, &current_version_vec, GG_STR("\0"));
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to copy information over");
        return false;
    }

    for (ulong index = 0; index < requirements_range.len; index++) {
        if (requirements_range_as_char[index] == ' ') {
            // null terminating as strverscmp requires it
            ret = gg_byte_vec_append(&work_mem_vec, GG_STR("\0"));
            if (ret != GG_ERR_OK) {
                GG_LOGE("Failed to copy information over");
                return false;
            }
            bool result = process_version(
                work_mem_vec, (char *) current_version_vec.buf.data
            );
            if (result == false) {
                GG_LOGT("Requirement wasn't satisfied");
                return false;
            }
            // Rest once a value is parsed
            work_mem_vec.buf.len = 0;
            index++;
        }
        ret = gg_byte_vec_append(
            &work_mem_vec,
            (GgBuffer) { (uint8_t *) &requirements_range_as_char[index], 1 }
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to copy information over");
            return false;
        }
    }

    if (work_mem_vec.buf.len != 0) {
        ret = gg_byte_vec_append(&work_mem_vec, GG_STR("\0"));
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to copy information over");
            return false;
        }
        bool result = process_version(
            work_mem_vec, (char *) current_version_vec.buf.data
        );
        if (result == false) {
            GG_LOGT("Requirement wasn't satisfied");
            return result;
        }
    }

    return true;
}
