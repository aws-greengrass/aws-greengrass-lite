// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <artifact_permission.h>
#include <assert.h>
#include <gg/buffer.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/nucleus/init.h>
#include <sys/types.h>

static void check(const char *name, mode_t actual, mode_t expected) {
    if (actual != expected) {
        GG_LOGE(
            "FAIL %s: expected 0%o, got 0%o",
            name,
            (unsigned) expected,
            (unsigned) actual
        );
        assert(actual == expected);
    }
    GG_LOGI("PASS %s: 0%o", name, (unsigned) actual);
}

int main(void) {
    ggl_nucleus_init();

    // Tests for artifact_permission_to_mode().
    //
    // This function is only called when the recipe has a Permission map.
    // When Permission is absent, deployment_handler.c uses 0755 as default
    // to avoid regression (Greengrass Nucleus defaults to 0440).
    //
    // Logic from Greengrass Nucleus Permission.java:
    //   owner always gets read.
    //   owner gets execute if Execute is OWNER or ALL.
    //   group is treated as owner (gets same read/execute as owner).
    //   group also gets read if execute is not NONE (execute implies read).
    //   other gets read if Read is ALL or Execute is ALL.
    //   other gets execute only if Execute is ALL.

    // Greengrass Nucleus default: 0440
    check(
        "read_owner_exec_none",
        artifact_permission_to_mode(GG_MAP(
            gg_kv(GG_STR("Read"), gg_obj_buf(GG_STR("OWNER"))),
            gg_kv(GG_STR("Execute"), gg_obj_buf(GG_STR("NONE")))
        )),
        0440
    );

    // other gets read from Read=ALL: 0444
    check(
        "read_all_exec_none",
        artifact_permission_to_mode(GG_MAP(
            gg_kv(GG_STR("Read"), gg_obj_buf(GG_STR("ALL"))),
            gg_kv(GG_STR("Execute"), gg_obj_buf(GG_STR("NONE")))
        )),
        0444
    );

    // owner+group get execute from Execute=OWNER: 0550
    check(
        "read_owner_exec_owner",
        artifact_permission_to_mode(GG_MAP(
            gg_kv(GG_STR("Read"), gg_obj_buf(GG_STR("OWNER"))),
            gg_kv(GG_STR("Execute"), gg_obj_buf(GG_STR("OWNER")))
        )),
        0550
    );

    // everyone gets read+execute: 0555
    check(
        "read_all_exec_all",
        artifact_permission_to_mode(GG_MAP(
            gg_kv(GG_STR("Read"), gg_obj_buf(GG_STR("ALL"))),
            gg_kv(GG_STR("Execute"), gg_obj_buf(GG_STR("ALL")))
        )),
        0555
    );

    // owner always reads even with Read=NONE: 0400
    check(
        "read_none_exec_none",
        artifact_permission_to_mode(GG_MAP(
            gg_kv(GG_STR("Read"), gg_obj_buf(GG_STR("NONE"))),
            gg_kv(GG_STR("Execute"), gg_obj_buf(GG_STR("NONE")))
        )),
        0400
    );

    // execute implies group read even with Read=NONE: 0550
    check(
        "read_none_exec_owner",
        artifact_permission_to_mode(GG_MAP(
            gg_kv(GG_STR("Read"), gg_obj_buf(GG_STR("NONE"))),
            gg_kv(GG_STR("Execute"), gg_obj_buf(GG_STR("OWNER")))
        )),
        0550
    );

    // other gets read from Read=ALL but not execute: 0554
    check(
        "read_all_exec_owner",
        artifact_permission_to_mode(GG_MAP(
            gg_kv(GG_STR("Read"), gg_obj_buf(GG_STR("ALL"))),
            gg_kv(GG_STR("Execute"), gg_obj_buf(GG_STR("OWNER")))
        )),
        0554
    );

    // Execute=ALL gives everyone read+execute even with Read=NONE: 0555
    check(
        "read_none_exec_all",
        artifact_permission_to_mode(GG_MAP(
            gg_kv(GG_STR("Read"), gg_obj_buf(GG_STR("NONE"))),
            gg_kv(GG_STR("Execute"), gg_obj_buf(GG_STR("ALL")))
        )),
        0555
    );

    // Empty Permission map (keys absent, not "NONE"): defaults to 0440
    check("empty_map", artifact_permission_to_mode((GgMap) { 0 }), 0440);

    GG_LOGI("All artifact permission tests passed.");
    return 0;
}
