// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "artifact_permission.h"
#include <gg/buffer.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stddef.h>

// Matches Greengrass Nucleus permission logic from:
// https://github.com/aws-greengrass/aws-greengrass-nucleus/blob/main/src/main/java/com/aws/greengrass/componentmanager/models/Permission.java
//
// Greengrass Nucleus treats group == owner and execute implies group read:
//   ownerRead:    always true
//   ownerExecute: execute == OWNER || execute == ALL
//   groupRead:    read != NONE || execute != NONE
//   groupExecute: execute == OWNER || execute == ALL
//   otherRead:    read == ALL || execute == ALL
//   otherExecute: execute == ALL
mode_t artifact_permission_to_mode(GgMap permission_map) {
    GgObject *read_obj = NULL;
    GgObject *execute_obj = NULL;

    gg_map_get(permission_map, GG_STR("Read"), &read_obj);
    gg_map_get(permission_map, GG_STR("Execute"), &execute_obj);

    bool read_all = (read_obj != NULL)
        && gg_buffer_eq(gg_obj_into_buf(*read_obj), GG_STR("ALL"));
    bool read_none = (read_obj != NULL)
        && gg_buffer_eq(gg_obj_into_buf(*read_obj), GG_STR("NONE"));
    bool exec_owner = (execute_obj != NULL)
        && gg_buffer_eq(gg_obj_into_buf(*execute_obj), GG_STR("OWNER"));
    bool exec_all = (execute_obj != NULL)
        && gg_buffer_eq(gg_obj_into_buf(*execute_obj), GG_STR("ALL"));

    mode_t mode = S_IRUSR;
    if (exec_owner || exec_all) {
        mode |= S_IXUSR;
    }
    if (!read_none || exec_owner || exec_all) {
        mode |= S_IRGRP;
    }
    if (exec_owner || exec_all) {
        mode |= S_IXGRP;
    }
    if (read_all || exec_all) {
        mode |= S_IROTH;
    }
    if (exec_all) {
        mode |= S_IXOTH;
    }
    return mode;
}

#ifdef GG_SDK_TESTING

#include <gg/test.h>
#include <unity.h>

GG_TEST_DEFINE(permission_read_owner_exec_none) {
    TEST_ASSERT_EQUAL_HEX16(
        0440,
        artifact_permission_to_mode(GG_MAP(
            gg_kv(GG_STR("Read"), gg_obj_buf(GG_STR("OWNER"))),
            gg_kv(GG_STR("Execute"), gg_obj_buf(GG_STR("NONE")))
        ))
    );
}

GG_TEST_DEFINE(permission_read_all_exec_none) {
    TEST_ASSERT_EQUAL_HEX16(
        0444,
        artifact_permission_to_mode(GG_MAP(
            gg_kv(GG_STR("Read"), gg_obj_buf(GG_STR("ALL"))),
            gg_kv(GG_STR("Execute"), gg_obj_buf(GG_STR("NONE")))
        ))
    );
}

GG_TEST_DEFINE(permission_read_owner_exec_owner) {
    TEST_ASSERT_EQUAL_HEX16(
        0550,
        artifact_permission_to_mode(GG_MAP(
            gg_kv(GG_STR("Read"), gg_obj_buf(GG_STR("OWNER"))),
            gg_kv(GG_STR("Execute"), gg_obj_buf(GG_STR("OWNER")))
        ))
    );
}

GG_TEST_DEFINE(permission_read_all_exec_all) {
    TEST_ASSERT_EQUAL_HEX16(
        0555,
        artifact_permission_to_mode(GG_MAP(
            gg_kv(GG_STR("Read"), gg_obj_buf(GG_STR("ALL"))),
            gg_kv(GG_STR("Execute"), gg_obj_buf(GG_STR("ALL")))
        ))
    );
}

GG_TEST_DEFINE(permission_read_none_exec_none) {
    TEST_ASSERT_EQUAL_HEX16(
        0400,
        artifact_permission_to_mode(GG_MAP(
            gg_kv(GG_STR("Read"), gg_obj_buf(GG_STR("NONE"))),
            gg_kv(GG_STR("Execute"), gg_obj_buf(GG_STR("NONE")))
        ))
    );
}

GG_TEST_DEFINE(permission_read_none_exec_owner) {
    TEST_ASSERT_EQUAL_HEX16(
        0550,
        artifact_permission_to_mode(GG_MAP(
            gg_kv(GG_STR("Read"), gg_obj_buf(GG_STR("NONE"))),
            gg_kv(GG_STR("Execute"), gg_obj_buf(GG_STR("OWNER")))
        ))
    );
}

GG_TEST_DEFINE(permission_read_all_exec_owner) {
    TEST_ASSERT_EQUAL_HEX16(
        0554,
        artifact_permission_to_mode(GG_MAP(
            gg_kv(GG_STR("Read"), gg_obj_buf(GG_STR("ALL"))),
            gg_kv(GG_STR("Execute"), gg_obj_buf(GG_STR("OWNER")))
        ))
    );
}

GG_TEST_DEFINE(permission_read_none_exec_all) {
    TEST_ASSERT_EQUAL_HEX16(
        0555,
        artifact_permission_to_mode(GG_MAP(
            gg_kv(GG_STR("Read"), gg_obj_buf(GG_STR("NONE"))),
            gg_kv(GG_STR("Execute"), gg_obj_buf(GG_STR("ALL")))
        ))
    );
}

GG_TEST_DEFINE(permission_empty_map) {
    TEST_ASSERT_EQUAL_HEX16(0440, artifact_permission_to_mode((GgMap) { 0 }));
}

#endif
