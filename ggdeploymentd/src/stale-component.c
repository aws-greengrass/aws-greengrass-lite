
#include "component_store.h"
#include "ggdeploymentd.h"
#include <sys/types.h>
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/exec.h>
#include <ggl/map.h>
#include <ggl/object.h>

GglError cleanup_stale_versions(GglMap latest_components_map) {
    int fd;
    static uint8_t mem[10000] = { 0 };
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(mem));
    GglMap current_components;

    GglError ret
        = retrieve_component_list(&fd, &balloc.alloc, &current_components);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_MAP_FOREACH(pair, current_components) {
        if (!ggl_map_get(latest_components_map, pair->key, NULL)) {
            char *exec_args[] = { "rm", "-r", (char *) pair->key.data, NULL };
            pid_t exec_pid = -1;
            exec_command_with_child_wait(exec_args, &exec_pid);
        }
    }
    return GGL_ERR_OK;
}
