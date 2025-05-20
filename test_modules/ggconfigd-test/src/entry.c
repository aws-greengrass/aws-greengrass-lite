
#include "ggconfigd-test.h"
#include "stdbool.h"
#include <assert.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/utils.h>
#include <ggl/vector.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#define SUCCESS_STRING "test-and-verify-the-world"

static const char *component_name = "sample";
static const char *component_version = "1.0.0";
const char *component_name_test = "ggconfigd-test";

GglError run_ggconfigd_test(void) {
    static uint8_t recipe_dir_mem[PATH_MAX] = { 0 };
    GglByteVec recipe_dir = GGL_BYTE_VEC(recipe_dir_mem);

    if (getcwd((char *) recipe_dir.buf.data, sizeof(recipe_dir_mem)) == NULL) {
        GGL_LOGE("Error getting current working directory.");
        assert(false);
        return GGL_ERR_FAILURE;
    }
    recipe_dir.buf.len = strlen((char *) recipe_dir.buf.data);

    GglError ret = ggl_byte_vec_append(
        &recipe_dir, GGL_STR("/ggconfigd-test/sample-recipe")
    );
    if (ret != GGL_ERR_OK) {
        assert(false);
        return ret;
    }

    GGL_LOGI(
        "Location of recipe file is %.*s",
        (int) recipe_dir.buf.len,
        recipe_dir.buf.data
    );

    GglKVVec args = GGL_KV_VEC((GglKV[3]) { 0 });

    ret = ggl_kv_vec_push(
        &args,
        ggl_kv(GGL_STR("recipe_directory_path"), ggl_obj_buf(recipe_dir.buf))
    );
    if (ret != GGL_ERR_OK) {
        assert(false);
        return ret;
    }

    GglKV component;
    if (component_name != NULL) {
        component = ggl_kv(
            ggl_buffer_from_null_term((char *) component_name),
            ggl_obj_buf(ggl_buffer_from_null_term((char *) component_version))
        );
        ret = ggl_kv_vec_push(
            &args,
            ggl_kv(
                GGL_STR("root_component_versions_to_add"),
                ggl_obj_map((GglMap) { .pairs = &component, .len = 1 })
            )
        );
        if (ret != GGL_ERR_OK) {
            assert(false);
            return ret;
        }
    }

    GglBuffer id_mem = GGL_BUF((uint8_t[36]) { 0 });
    GglArena alloc = ggl_arena_init(id_mem);

    ret = ggl_call(
        GGL_STR("gg_deployment"),
        GGL_STR("create_local_deployment"),
        args.map,
        NULL,
        &alloc,
        NULL
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // Hacky way to wait for deployment. Once we have an API to verify that a
    // given deployment is complete, we should use that.
    (void) ggl_sleep(10);

    // find the version of the active running component
    GglObject result_obj;
    static uint8_t version_resp_mem[10024] = { 0 };
    GglArena version_alloc = ggl_arena_init(GGL_BUF(version_resp_mem));

    ret = ggl_gg_config_read(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("com.example.sample"),
            GGL_STR("message")
        ),
        &version_alloc,
        &result_obj
    );

    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (ggl_obj_type(result_obj) != GGL_TYPE_BUF) {
        GGL_LOGE("Result is not a buffer.");
        return GGL_ERR_FAILURE;
    }

    GglBuffer result = ggl_obj_into_buf(result_obj);
    size_t min = strlen(SUCCESS_STRING);
    if (min > result.len) {
        min = result.len;
    }

    if ((strlen(SUCCESS_STRING) != result.len)
        || (strncmp(SUCCESS_STRING, (const char *) result.data, min) != 0)) {
        GGL_LOGE("Test failed");
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}
