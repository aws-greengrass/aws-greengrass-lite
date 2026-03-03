
#include <assert.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/utils.h>
#include <gg/vector.h>
#include <ggconfigd-test.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

#define SUCCESS_STRING "test-and-verify-the-world"

static const char *component_name = "sample";
static const char *component_version = "1.0.0";
const char *component_name_test = "ggconfigd-test";

GgError run_ggconfigd_test(void) {
    // Backup/restore round-trip test
    GgError ret = ggl_gg_config_write(
        GG_BUF_LIST(GG_STR("test"), GG_STR("backup_key")),
        gg_obj_buf(GG_STR("original")),
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("backup test: write original failed");
        return ret;
    }

    ret = ggl_gg_config_backup();
    if (ret != GG_ERR_OK) {
        GG_LOGE("backup test: backup failed");
        return ret;
    }

    ret = ggl_gg_config_write(
        GG_BUF_LIST(GG_STR("test"), GG_STR("backup_key")),
        gg_obj_buf(GG_STR("modified")),
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("backup test: write modified failed");
        return ret;
    }

    ret = ggl_gg_config_restore();
    if (ret != GG_ERR_OK) {
        GG_LOGE("backup test: restore failed");
        return ret;
    }

    static uint8_t backup_read_mem[256];
    GgArena backup_alloc = gg_arena_init(GG_BUF(backup_read_mem));
    GgBuffer backup_result;
    ret = ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("test"), GG_STR("backup_key")),
        &backup_alloc,
        &backup_result
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("backup test: read after restore failed");
        return ret;
    }

    if (!gg_buffer_eq(backup_result, GG_STR("original"))) {
        GG_LOGE(
            "backup test: expected \"original\", got \"%.*s\"",
            (int) backup_result.len,
            backup_result.data
        );
        return GG_ERR_FAILURE;
    }

    GG_LOGI("backup/restore test passed");

    // Existing deployment test
    static uint8_t recipe_dir_mem[PATH_MAX] = { 0 };
    GgByteVec recipe_dir = GG_BYTE_VEC(recipe_dir_mem);

    if (getcwd((char *) recipe_dir.buf.data, sizeof(recipe_dir_mem)) == NULL) {
        GG_LOGE("Error getting current working directory.");
        assert(false);
        return GG_ERR_FAILURE;
    }
    recipe_dir.buf.len = strlen((char *) recipe_dir.buf.data);

    ret = gg_byte_vec_append(
        &recipe_dir, GG_STR("/ggconfigd-test/sample-recipe")
    );
    if (ret != GG_ERR_OK) {
        assert(false);
        return ret;
    }

    GG_LOGI(
        "Location of recipe file is %.*s",
        (int) recipe_dir.buf.len,
        recipe_dir.buf.data
    );

    GgKVVec args = GG_KV_VEC((GgKV[3]) { 0 });

    ret = gg_kv_vec_push(
        &args,
        gg_kv(GG_STR("recipe_directory_path"), gg_obj_buf(recipe_dir.buf))
    );
    if (ret != GG_ERR_OK) {
        assert(false);
        return ret;
    }

    GgKV component;
    if (component_name != NULL) {
        component = gg_kv(
            gg_buffer_from_null_term((char *) component_name),
            gg_obj_buf(gg_buffer_from_null_term((char *) component_version))
        );
        ret = gg_kv_vec_push(
            &args,
            gg_kv(
                GG_STR("root_component_versions_to_add"),
                gg_obj_map((GgMap) { .pairs = &component, .len = 1 })
            )
        );
        if (ret != GG_ERR_OK) {
            assert(false);
            return ret;
        }
    }

    GgBuffer id_mem = GG_BUF((uint8_t[36]) { 0 });
    GgArena alloc = gg_arena_init(id_mem);

    ret = ggl_call(
        GG_STR("gg_deployment"),
        GG_STR("create_local_deployment"),
        args.map,
        NULL,
        &alloc,
        NULL
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // Hacky way to wait for deployment. Once we have an API to verify that a
    // given deployment is complete, we should use that.
    (void) gg_sleep(10);

    // find the version of the active running component
    GgObject result_obj;
    static uint8_t version_resp_mem[10024] = { 0 };
    GgArena version_alloc = gg_arena_init(GG_BUF(version_resp_mem));

    ret = ggl_gg_config_read(
        GG_BUF_LIST(
            GG_STR("services"), GG_STR("com.example.sample"), GG_STR("message")
        ),
        &version_alloc,
        &result_obj
    );

    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (gg_obj_type(result_obj) != GG_TYPE_BUF) {
        GG_LOGE("Result is not a buffer.");
        return GG_ERR_FAILURE;
    }

    GgBuffer result = gg_obj_into_buf(result_obj);
    size_t min = strlen(SUCCESS_STRING);
    if (min > result.len) {
        min = result.len;
    }

    if ((strlen(SUCCESS_STRING) != result.len)
        || (strncmp(SUCCESS_STRING, (const char *) result.data, min) != 0)) {
        GG_LOGE("Test failed");
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}
