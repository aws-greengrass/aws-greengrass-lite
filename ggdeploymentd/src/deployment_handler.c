// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "deployment_handler.h"
#include "deployment_model.h"
#include "deployment_queue.h"
#include <sys/types.h>
#include <fcntl.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static GglError merge_dir_to(
    GglBuffer source, int root_path_fd, GglBuffer subdir
) {
    int source_fd;
    GglError ret = ggl_dir_open(source, O_PATH, &source_fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_DEFER(ggl_close, source_fd);

    int dest_fd;
    ret = ggl_dir_openat(root_path_fd, subdir, O_PATH, &dest_fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_DEFER(ggl_close, dest_fd);

    return ggl_copy_dir(source_fd, dest_fd);
}

static GglError get_thing_name(char **thing_name) {
    GglMap params = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("thingName")) }
    );

    static uint8_t resp_mem[128] = { 0 };
    GglBumpAlloc balloc
        = ggl_bump_alloc_init((GglBuffer) { .data = resp_mem, .len = 127 });

    GglObject resp;
    GglError ret = ggl_call(
        GGL_STR("gg_config"),
        GGL_STR("read"),
        params,
        NULL,
        &balloc.alloc,
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("ggdeploymentd", "Failed to get thing name from config.");
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE("ggdeploymentd", "Configuration thing name is not a string.");
        return GGL_ERR_INVALID;
    }

    resp.buf.data[resp.buf.len] = '\0';
    *thing_name = (char *) resp.buf.data;
    return GGL_ERR_OK;
}

static GglError get_region(char **region) {
    GglMap params = GGL_MAP({ GGL_STR("key_path"),
                              GGL_OBJ_LIST(
                                  GGL_OBJ_STR("services"),
                                  GGL_OBJ_STR("aws.greengrass.Nucleus-Lite"),
                                  GGL_OBJ_STR("configuration"),
                                  GGL_OBJ_STR("awsRegion")
                              ) });

    static uint8_t resp_mem[128] = { 0 };
    GglBumpAlloc balloc
        = ggl_bump_alloc_init((GglBuffer) { .data = resp_mem, .len = 127 });

    GglObject resp;
    GglError ret = ggl_call(
        GGL_STR("gg_config"),
        GGL_STR("read"),
        params,
        NULL,
        &balloc.alloc,
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("ggdeploymentd", "Failed to get region from config.");
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE("ggdeploymentd", "Configuration region is not a string.");
        return GGL_ERR_INVALID;
    }

    resp.buf.data[resp.buf.len] = '\0';
    *region = (char *) resp.buf.data;
    return GGL_ERR_OK;
}

static GglError get_root_ca_path(char **root_ca_path) {
    GglMap params = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("rootCaPath")) }
    );

    static uint8_t resp_mem[128] = { 0 };
    GglBumpAlloc balloc
        = ggl_bump_alloc_init((GglBuffer) { .data = resp_mem, .len = 127 });

    GglObject resp;
    GglError ret = ggl_call(
        GGL_STR("gg_config"),
        GGL_STR("read"),
        params,
        NULL,
        &balloc.alloc,
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("ggdeploymentd", "Failed to get rootCaPath from config.");
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE("ggdeploymentd", "Configuration rootCaPath is not a string.");
        return GGL_ERR_INVALID;
    }

    resp.buf.data[resp.buf.len] = '\0';
    *root_ca_path = (char *) resp.buf.data;
    return GGL_ERR_OK;
}

static GglError get_tes_cred_url(char **tes_cred_url) {
    GglMap params = GGL_MAP({ GGL_STR("key_path"),
                              GGL_OBJ_LIST(
                                  GGL_OBJ_STR("services"),
                                  GGL_OBJ_STR("aws.greengrass.Nucleus-Lite"),
                                  GGL_OBJ_STR("configuration"),
                                  GGL_OBJ_STR("tesCredUrl")
                              ) });

    static uint8_t resp_mem[128] = { 0 };
    GglBumpAlloc balloc
        = ggl_bump_alloc_init((GglBuffer) { .data = resp_mem, .len = 127 });

    GglObject resp;
    GglError ret = ggl_call(
        GGL_STR("gg_config"),
        GGL_STR("read"),
        params,
        NULL,
        &balloc.alloc,
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW(
            "ggdeploymentd", "Failed to get tes credentials url from config."
        );
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE(
            "ggdeploymentd",
            "Configuration tes credentials url is not a string."
        );
        return GGL_ERR_INVALID;
    }

    resp.buf.data[resp.buf.len] = '\0';
    *tes_cred_url = (char *) resp.buf.data;
    return GGL_ERR_OK;
}

static GglError get_posix_user(char **posix_user) {
    GglMap params = GGL_MAP({ GGL_STR("key_path"),
                              GGL_OBJ_LIST(
                                  GGL_OBJ_STR("services"),
                                  GGL_OBJ_STR("aws.greengrass.Nucleus-Lite"),
                                  GGL_OBJ_STR("configuration"),
                                  GGL_OBJ_STR("runWithDefault"),
                                  GGL_OBJ_STR("posixUser")
                              ) });

    static uint8_t resp_mem[128] = { 0 };
    GglBumpAlloc balloc
        = ggl_bump_alloc_init((GglBuffer) { .data = resp_mem, .len = 127 });

    GglObject resp;
    GglError ret = ggl_call(
        GGL_STR("gg_config"),
        GGL_STR("read"),
        params,
        NULL,
        &balloc.alloc,
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("ggdeploymentd", "Failed to get posix user from config.");
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE("ggdeploymentd", "Configuration posix user is not a string.");
        return GGL_ERR_INVALID;
    }

    resp.buf.data[resp.buf.len] = '\0';
    *posix_user = (char *) resp.buf.data;
    return GGL_ERR_OK;
}

static void handle_deployment(
    GglDeployment *deployment, GglDeploymentHandlerThreadArgs *args
) {
    int root_path_fd = args->root_path_fd;
    if (deployment->recipe_directory_path.len != 0) {
        GglError ret = merge_dir_to(
            deployment->recipe_directory_path,
            root_path_fd,
            GGL_STR("/packages/recipes")
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("ggdeploymentd", "Failed to copy recipes.");
            return;
        }
    }

    if (deployment->artifact_directory_path.len != 0) {
        GglError ret = merge_dir_to(
            deployment->artifact_directory_path,
            root_path_fd,
            GGL_STR("/packages/artifacts")
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("ggdeploymentd", "Failed to copy artifacts.");
            return;
        }
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    char *recipe2unit_path = getenv("GGL_RECIPE_TO_UNIT_PATH");
    if (deployment->root_component_versions_to_add.len != 0) {
        GGL_MAP_FOREACH(pair, deployment->root_component_versions_to_add) {
            // python3 ./recipe2unit/main.py -r
            // ~/repo/lite-dev/sample-component/recipes/com.example.SampleComponent-1.0.0.yaml
            // -e ./ -s ./gg-ipc.socket --user aa --group aaa --artifact-path
            // /var/lib/aws-greengrass-v2 -t thingname
            if (pair->val.type != GGL_TYPE_BUF) {
                GGL_LOGE("ggdeploymentd", "Component version is not a buffer.");
                return;
            }

            char recipe_path[256] = { 0 };
            strncat(
                recipe_path, (char *) args->root_path.data, args->root_path.len
            );
            strncat(
                recipe_path, "/packages/recipes/", strlen("/packages/recipes/")
            );
            strncat(recipe_path, (char *) pair->key.data, pair->key.len);
            strncat(recipe_path, "-", strlen("-"));
            strncat(
                recipe_path, (char *) pair->val.buf.data, pair->val.buf.len
            );
            strncat(recipe_path, ".yaml", strlen(".yaml"));

            char recipe_runner_path[256] = { 0 };
            strncat(recipe_runner_path, args->bin_path, strlen(args->bin_path));
            strncat(
                recipe_runner_path, "recipe-runner", strlen("recipe-runner")
            );

            char socket_path[256] = { 0 };
            strncat(
                socket_path, (char *) args->root_path.data, args->root_path.len
            );
            strncat(socket_path, "/gg-ipc.socket", strlen("/gg-ipc.socket"));

            char *thing_name = NULL;
            GglError ret = get_thing_name(&thing_name);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("ggdeploymentd", "Failed to get thing name.");
                return;
            }

            char *region = NULL;
            ret = get_region(&region);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("ggdeploymentd", "Failed to get region.");
                return;
            }

            char *root_ca_path = NULL;
            ret = get_root_ca_path(&root_ca_path);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("ggdeploymentd", "Failed to get rootCaPath.");
                return;
            }

            char *tes_cred_url = NULL;
            ret = get_tes_cred_url(&tes_cred_url);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("ggdeploymentd", "Failed to get tes credentials url.");
                return;
            }

            char *posix_user = NULL;
            ret = get_posix_user(&posix_user);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("ggdeploymentd", "Failed to get posix_user.");
                return;
            }
            if (strlen(posix_user) < 1) {
                GGL_LOGE(
                    "ggdeploymentd", "Run with default posix user is not set."
                );
                return;
            }
            bool colon_found = false;
            char *group;
            for (size_t i = 0; i < strlen(posix_user); i++) {
                if (posix_user[i] == ':') {
                    posix_user[i] = '\0';
                    colon_found = true;
                    group = &posix_user[i + 1];
                    break;
                }
            }
            if (!colon_found) {
                group = posix_user;
            }

            char artifact_path[256] = { 0 };
            strncat(
                artifact_path,
                (char *) args->root_path.data,
                args->root_path.len
            );
            strncat(
                artifact_path,
                "/packages/artifacts/",
                strlen("/packages/artifacts/")
            );
            strncat(artifact_path, (char *) pair->key.data, pair->key.len);
            strncat(artifact_path, "/", strlen("/"));
            strncat(
                artifact_path, (char *) pair->val.buf.data, pair->val.buf.len
            );

            char *recipe2unit_args[] = { "python3",
                                         recipe2unit_path,
                                         "-r",
                                         recipe_path,
                                         "--recipe-runner-path",
                                         recipe_runner_path,
                                         "--socket-path",
                                         socket_path,
                                         "-t",
                                         thing_name,
                                         "--aws-region",
                                         region,
                                         "--ggc-version",
                                         "0.0.1",
                                         "--rootca-path",
                                         root_ca_path,
                                         "--cred-url",
                                         tes_cred_url,
                                         "--user",
                                         posix_user,
                                         "--group",
                                         group,
                                         "--artifact-path",
                                         artifact_path,
                                         "--root-dir",
                                         (char *) args->root_path.data };

            pid_t pid = fork();

            if (pid == -1) {
                // Something went wrong

                GGL_LOGE("ggdeploymentd", "Error, Unable to fork");
                return;
            }
            if (pid == 0) {
                // Child process: execute the script
                execvp("python3", recipe2unit_args);

                // If execvpe returns, it must have failed
                GGL_LOGE(
                    "ggdeploymentd", "Error: execvp returned unexpectedly"
                );
                return;
            }
            // Parent process: wait for the child to finish

            int child_status;
            if (waitpid(pid, &child_status, 0) == -1) {
                GGL_LOGE("ggdeploymentd", "Error, waitpid got hit");
                return;
            }
            if (WIFEXITED(child_status)) {
                if (WEXITSTATUS(child_status) != 0) {
                    GGL_LOGE("ggdeploymentd", "Recipe to unit script failed");
                    return;
                }
                GGL_LOGI(
                    "ggdeploymentd",
                    "Recipe to unit script exited with child status "
                    "%d\n",
                    WEXITSTATUS(child_status)
                );
            } else {
                GGL_LOGE(
                    "ggdeploymentd",
                    "Recipe to unit script did not exit normally"
                );
                return;
            }

            char install_file_path[256] = { 0 };
            strncat(install_file_path, "ggl.", strlen("ggl."));
            strncat(install_file_path, (char *) pair->key.data, pair->key.len);
            strncat(
                install_file_path, ".script.install", strlen(".script.install")
            );
            int open_ret = open(install_file_path, O_RDONLY | O_CLOEXEC);
            if (open_ret < 0) {
                GGL_LOGE("ggdeploymentd", "Could not find install file.");
                return;
            }

            char run_file_path[256] = { 0 };
            strncat(run_file_path, "ggl.", strlen("ggl."));
            strncat(run_file_path, (char *) pair->key.data, pair->key.len);
            strncat(run_file_path, ".script.run", strlen(".script.run"));
            open_ret = open(run_file_path, O_RDONLY | O_CLOEXEC);
            if (open_ret < 0) {
                GGL_LOGE("ggdeploymentd", "Could not find run file.");
                return;
            }

            char service_file_path[256] = { 0 };
            strncat(service_file_path, "ggl.", strlen("ggl."));
            strncat(service_file_path, (char *) pair->key.data, pair->key.len);
            strncat(service_file_path, ".service", strlen(".service"));
            open_ret = open(service_file_path, O_RDONLY | O_CLOEXEC);
            if (open_ret < 0) {
                GGL_LOGE("ggdeploymentd", "Could not find service file.");
                return;
            }

            char dot_slash_install[256] = { 0 };
            strncat(dot_slash_install, "./", strlen("./"));
            strncat(
                dot_slash_install, install_file_path, strlen(install_file_path)
            );
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            int system_ret = system(dot_slash_install);
            if (WIFEXITED(system_ret)) {
                if (WEXITSTATUS(system_ret) != 0) {
                    GGL_LOGE("ggdeploymentd", "Install script failed");
                    return;
                }
                GGL_LOGI(
                    "ggdeploymentd",
                    "Install script exited with child status %d\n",
                    WEXITSTATUS(system_ret)
                );
            } else {
                GGL_LOGE(
                    "ggdeploymentd", "Install script did not exit normally"
                );
                return;
            }

            char link_command[256] = { 0 };
            strncat(
                link_command,
                "sudo systemctl link ",
                strlen("sudo systemctl link ")
            );
            char cwd[256];
            if (getcwd(cwd, sizeof(cwd)) == NULL) {
                GGL_LOGE(
                    "ggdeploymentd", "Error getting current working directory."
                );
                return;
            }
            strncat(link_command, cwd, strlen(cwd));
            strncat(link_command, "/", strlen("/"));
            strncat(link_command, service_file_path, strlen(service_file_path));
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            system_ret = system(link_command);
            if (WIFEXITED(system_ret)) {
                if (WEXITSTATUS(system_ret) != 0) {
                    GGL_LOGE("ggdeploymentd", "systemctl link failed");
                    return;
                }
                GGL_LOGI(
                    "ggdeploymentd",
                    "systemctl link exited with child status %d\n",
                    WEXITSTATUS(system_ret)
                );
            } else {
                GGL_LOGE(
                    "ggdeploymentd", "systemctl link did not exit normally"
                );
                return;
            }

            char start_command[256] = { 0 };
            strncat(
                start_command,
                "sudo systemctl start ",
                strlen("sudo systemctl start ")
            );
            strncat(
                start_command, service_file_path, strlen(service_file_path)
            );
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            system_ret = system(start_command);
            if (WIFEXITED(system_ret)) {
                if (WEXITSTATUS(system_ret) != 0) {
                    GGL_LOGE("ggdeploymentd", "systemctl start failed");
                    return;
                }
                GGL_LOGI(
                    "ggdeploymentd",
                    "systemctl start exited with child status %d\n",
                    WEXITSTATUS(system_ret)
                );
            } else {
                GGL_LOGE(
                    "ggdeploymentd", "systemctl start did not exit normally"
                );
                return;
            }

            char enable_command[256] = { 0 };
            strncat(
                enable_command,
                "sudo systemctl enable ",
                strlen("sudo systemctl enable ")
            );
            strncat(
                enable_command, service_file_path, strlen(service_file_path)
            );
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            system_ret = system(enable_command);
            if (WIFEXITED(system_ret)) {
                if (WEXITSTATUS(system_ret) != 0) {
                    GGL_LOGE("ggdeploymentd", "systemctl enable failed");
                    return;
                }
                GGL_LOGI(
                    "ggdeploymentd",
                    "systemctl enable exited with child status %d\n",
                    WEXITSTATUS(system_ret)
                );
            } else {
                GGL_LOGE(
                    "ggdeploymentd", "systemctl enable did not exit normally"
                );
                return;
            }
        }
    }
}

static GglError ggl_deployment_listen(GglDeploymentHandlerThreadArgs *args) {
    while (true) {
        GglDeployment *deployment;
        // Since this is blocking, error is fatal
        GglError ret = ggl_deployment_dequeue(&deployment);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        GGL_LOGI("deployment-handler", "Processing incoming deployment.");

        handle_deployment(deployment, args);

        GGL_LOGD("deployment-handler", "Completed deployment.");

        ggl_deployment_release(deployment);
    }
}

void *ggl_deployment_handler_thread(void *ctx) {
    GGL_LOGD("deployment-handler", "Starting deployment processing thread.");

    (void) ggl_deployment_listen(ctx);

    GGL_LOGE("deployment-handler", "Deployment thread exiting due to failure.");

    // This is safe as long as only this thread will ever call exit.

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);

    return NULL;
}
