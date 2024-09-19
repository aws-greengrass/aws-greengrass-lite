// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "deployment_handler.h"
#include "deployment_model.h"
#include "deployment_queue.h"
#include "iot_jobs_listener.h"
#include <sys/types.h>
#include <fcntl.h>
#include <ggl/base64.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/http.h>
#include <ggl/json_decode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/socket.h>
#include <ggl/vector.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static struct DeploymentConfiguration {
    char port[16];
    char data_endpoint[128];
    char cert_path[128];
    char rootca_path[128];
    char pkey_path[128];
} config;

static GglError merge_dir_to(
    GglBuffer source, int root_path_fd, GglBuffer subdir
) {
    int source_fd;
    GglError ret = ggl_dir_open(source, O_PATH, false, &source_fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_DEFER(ggl_close, source_fd);

    int dest_fd;
    ret = ggl_dir_openat(root_path_fd, subdir, O_RDONLY, true, &dest_fd);
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

static GglError get_data_endpoint(GglByteVec *endpoint) {
    GglMap params = GGL_MAP({ GGL_STR("key_path"),
                              GGL_OBJ_LIST(
                                  GGL_OBJ_STR("services"),
                                  GGL_OBJ_STR("aws.greengrass.Nucleus-Lite"),
                                  GGL_OBJ_STR("configuration"),
                                  GGL_OBJ_STR("iotDataEndpoint")
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
            "ggdeploymentd", "Failed to get dataplane endpoint from config."
        );
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE(
            "ggdeploymentd", "Configuration dataplane endpoint is not a string."
        );
        return GGL_ERR_INVALID;
    }

    return ggl_byte_vec_append(endpoint, resp.buf);
}

static GglError get_data_port(GglByteVec *port) {
    GglMap params = GGL_MAP({ GGL_STR("key_path"),
                              GGL_OBJ_LIST(
                                  GGL_OBJ_STR("services"),
                                  GGL_OBJ_STR("aws.greengrass.Nucleus-Lite"),
                                  GGL_OBJ_STR("configuration"),
                                  GGL_OBJ_STR("greengrassDataPlanePort")
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
        GGL_LOGW("ggdeploymentd", "Failed to get dataplane port from config.");
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE(
            "ggdeploymentd", "Configuration dataplane port is not a string."
        );
        return GGL_ERR_INVALID;
    }

    return ggl_byte_vec_append(port, resp.buf);
}

static GglError get_private_key_path(GglByteVec *pkey_path) {
    GglMap params = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("privateKeyPath")) }
    );

    uint8_t resp_mem[128] = { 0 };
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
            "ggdeploymentd", "Failed to get private key path from config."
        );
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE(
            "ggdeploymentd", "Configuration private key path is not a string."
        );
        return GGL_ERR_INVALID;
    }

    ggl_byte_vec_chain_append(&ret, pkey_path, resp.buf);
    ggl_byte_vec_chain_push(&ret, pkey_path, '\0');
    return ret;
}

static GglError get_cert_path(GglByteVec *cert_path) {
    GglMap params = GGL_MAP({ GGL_STR("key_path"),
                              GGL_OBJ_LIST(
                                  GGL_OBJ_STR("system"),
                                  GGL_OBJ_STR("certificateFilePath")
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
            "ggdeploymentd", "Failed to get certificate path from config."
        );
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE(
            "ggdeploymentd", "Configuration certificate path is not a string."
        );
        return GGL_ERR_INVALID;
    }

    ggl_byte_vec_chain_append(&ret, cert_path, resp.buf);
    ggl_byte_vec_chain_push(&ret, cert_path, '\0');
    return ret;
}

static GglError get_rootca_path(GglByteVec *rootca_path) {
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
        GGL_LOGW("ggdeploymentd", "Failed to get rootca path from config.");
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE("ggdeploymentd", "Configuration rootca path is not a string.");
        return GGL_ERR_INVALID;
    }

    ggl_byte_vec_chain_append(&ret, rootca_path, resp.buf);
    ggl_byte_vec_chain_push(&ret, rootca_path, '\0');
    return ret;
}

// This will be refactored soon with recipe2unit in c, so ignore this warning
// for now

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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

    // TODO: Add dependency resolution process that will also check local store.
    if (deployment->cloud_root_components_to_add.len != 0) {
        static char resolve_candidates_body_buf[2048];
        GglByteVec body_vec = GGL_BYTE_VEC(resolve_candidates_body_buf);
        GglError byte_vec_ret = GGL_ERR_OK;
        ggl_byte_vec_chain_append(
            &byte_vec_ret, &body_vec, GGL_STR("{\"componentCandidates\": [")
        );

        bool is_first = true;
        // TODO: Are we doing one cloud call or an individual one per component?
        // The API supports multiple but GG Classic seems to do individual.
        GGL_MAP_FOREACH(pair, deployment->cloud_root_components_to_add) {
            if (pair->val.type != GGL_TYPE_MAP) {
                GGL_LOGE(
                    "ggdeploymentd",
                    "Incorrect formatting for cloud deployment components "
                    "field."
                );
                return;
            }

            GglObject *val;
            GglBuffer component_version = { 0 };
            if (ggl_map_get(pair->val.map, GGL_STR("version"), &val)) {
                if (val->type != GGL_TYPE_BUF) {
                    GGL_LOGE("ggdeploymentd", "Received invalid argument.");
                    return;
                }
                component_version = val->buf;
            }

            if (!is_first) {
                ggl_byte_vec_chain_push(&byte_vec_ret, &body_vec, ',');
            }
            is_first = false;

            ggl_byte_vec_chain_append(
                &byte_vec_ret, &body_vec, GGL_STR("{\"componentName\": \"")
            );
            ggl_byte_vec_chain_append(&byte_vec_ret, &body_vec, pair->key);
            ggl_byte_vec_chain_append(
                &byte_vec_ret, &body_vec, GGL_STR("\",\"componentVersion\": \"")
            );
            ggl_byte_vec_chain_append(
                &byte_vec_ret, &body_vec, component_version
            );
            // TODO: Version requirements by thing group during dependency
            // resolution
            ggl_byte_vec_chain_append(
                &byte_vec_ret,
                &body_vec,
                GGL_STR("\", \"versionRequirements\": {}}")
            );
        }

        ggl_byte_vec_chain_append(
            &byte_vec_ret,
            &body_vec,
            GGL_STR("],\"platform\": { \"attributes\": { \"os\" : \"linux\" "
                    "},\"name\": \"linux\"}}")
        );
        ggl_byte_vec_chain_push(&byte_vec_ret, &body_vec, '\0');

        GGL_LOGD("ggdeploymentd", "Body for call: %s", body_vec.buf.data);

        GglByteVec data_endpoint = GGL_BYTE_VEC(config.data_endpoint);
        GglError ret = get_data_endpoint(&data_endpoint);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("ggdeploymentd", "Failed to get dataplane endpoint.");
            return;
        }

        GglByteVec port = GGL_BYTE_VEC(config.port);
        ret = get_data_port(&port);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("ggdeploymentd", "Failed to get dataplane port.");
            return;
        }

        GglByteVec pkey_path = GGL_BYTE_VEC(config.pkey_path);
        ret = get_private_key_path(&pkey_path);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("ggdeploymentd", "Failed to get private key path.");
            return;
        }

        GglByteVec cert_path = GGL_BYTE_VEC(config.cert_path);
        ret = get_cert_path(&cert_path);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("ggdeploymentd", "Failed to get certificate path.");
            return;
        }

        GglByteVec rootca_path = GGL_BYTE_VEC(config.rootca_path);
        ret = get_rootca_path(&rootca_path);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("ggdeploymentd", "Failed to get certificate path.");
            return;
        }

        CertificateDetails cert_details
            = { .gghttplib_cert_path = config.cert_path,
                .gghttplib_root_ca_path = config.rootca_path,
                .gghttplib_p_key_path = config.pkey_path };

        static uint8_t component_candidates_response_buf[16384] = { 0 };
        GglBuffer ggl_component_candidates_response_buf
            = GGL_BUF(component_candidates_response_buf);

        gg_dataplane_call(
            data_endpoint.buf,
            port.buf,
            GGL_STR("greengrass/v2/resolveComponentCandidates"),
            cert_details,
            resolve_candidates_body_buf,
            &ggl_component_candidates_response_buf
        );

        GGL_LOGD(
            "ggdeploymentd",
            "Received response from resolveComponentCandidates: %.*s",
            (int) ggl_component_candidates_response_buf.len,
            ggl_component_candidates_response_buf.data
        );

        bool is_empty_response = ggl_buffer_eq(
            ggl_component_candidates_response_buf, GGL_STR("{}")
        );

        if (is_empty_response) {
            GGL_LOGI(
                "ggdeploymentd",
                "No suitable candidate found for component, skipping this "
                "component installation."
            );
        }

        if (!is_empty_response) {
            GglObject json_candidates_response_obj;
            // TODO: Figure out a better size. This response can be big.
            uint8_t candidates_response_mem[100 * sizeof(GglObject)];
            GglBumpAlloc balloc
                = ggl_bump_alloc_init(GGL_BUF(candidates_response_mem));
            ret = ggl_json_decode_destructive(
                ggl_component_candidates_response_buf,
                &balloc.alloc,
                &json_candidates_response_obj
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "ggdeploymentd",
                    "Error when parsing resolveComponentCandidates response to "
                    "json."
                );
                return;
            }

            if (json_candidates_response_obj.type != GGL_TYPE_MAP) {
                GGL_LOGE(
                    "ggdeploymentd",
                    "resolveComponentCandidates response did not parse into a "
                    "map."
                );
                return;
            }

            GglObject *resolved_component_versions;
            if (ggl_map_get(
                    json_candidates_response_obj.map,
                    GGL_STR("resolvedComponentVersions"),
                    &resolved_component_versions
                )) {
                if (resolved_component_versions->type != GGL_TYPE_LIST) {
                    GGL_LOGE(
                        "ggdeploymentd",
                        "resolvedComponentVersions response is not a list."
                    );
                    return;
                }
            }

            for (size_t i = 0; i < resolved_component_versions->list.len; i++) {
                GglObject resolved_version
                    = resolved_component_versions->list.items[i];

                if (resolved_version.type != GGL_TYPE_MAP) {
                    GGL_LOGE(
                        "ggdeploymentd", "Resolved version is not of type map."
                    );
                    return;
                }

                GglObject *cloud_component_name;
                if (ggl_map_get(
                        resolved_version.map,
                        GGL_STR("componentName"),
                        &cloud_component_name
                    )) {
                    if (cloud_component_name->type != GGL_TYPE_BUF) {
                        GGL_LOGE(
                            "ggdeploymentd",
                            "Resolved cloud component name is not a buffer."
                        );
                        return;
                    }
                }

                GglObject *cloud_component_version;
                if (ggl_map_get(
                        resolved_version.map,
                        GGL_STR("componentVersion"),
                        &cloud_component_version
                    )) {
                    if (cloud_component_version->type != GGL_TYPE_BUF) {
                        GGL_LOGE(
                            "ggdeploymentd",
                            "Resolved cloud component version is not a buffer."
                        );
                        return;
                    }
                }

                GglObject *vendor_guidance;
                if (ggl_map_get(
                        resolved_version.map,
                        GGL_STR("componentVersion"),
                        &vendor_guidance
                    )) {
                    if (vendor_guidance->type != GGL_TYPE_BUF) {
                        GGL_LOGE(
                            "ggdeploymentd",
                            "Resolved cloud component vendor guidance is not a "
                            "buffer."
                        );
                        return;
                    }
                    if (ggl_buffer_eq(
                            vendor_guidance->buf, GGL_STR("DISCONTINUED")
                        )) {
                        GGL_LOGW(
                            "ggdeploymentd",
                            "The component version has been discontinued by "
                            "its "
                            "publisher. You can deploy this component version, "
                            "but "
                            "we recommend that you use a different version of "
                            "this "
                            "component"
                        );
                    }
                }

                GglObject *recipe_file_content;
                if (ggl_map_get(
                        resolved_version.map,
                        GGL_STR("recipe"),
                        &recipe_file_content
                    )) {
                    if (recipe_file_content->type != GGL_TYPE_BUF) {
                        GGL_LOGE(
                            "ggdeploymentd",
                            "Resolved cloud component recipe data is not a "
                            "buffer."
                        );
                        return;
                    }
                }

                ggl_base64_decode_in_place(&(recipe_file_content->buf));

                GGL_LOGD(
                    "ggdeploymentd",
                    "Decoded recipe data as: %.*s",
                    (int) recipe_file_content->buf.len,
                    recipe_file_content->buf.data
                );

                static uint8_t recipe_name_buf[256];
                GglByteVec recipe_name_vec = GGL_BYTE_VEC(recipe_name_buf);
                ret = ggl_byte_vec_append(
                    &recipe_name_vec, cloud_component_name->buf
                );
                ggl_byte_vec_chain_append(&ret, &recipe_name_vec, GGL_STR("-"));
                ggl_byte_vec_chain_append(
                    &ret, &recipe_name_vec, cloud_component_version->buf
                );
                // TODO: Actual support for .json files. We're writing a .json
                // to a .yaml and relying on yaml being an almost-superset of
                // json.
                ggl_byte_vec_chain_append(
                    &ret, &recipe_name_vec, GGL_STR(".yaml")
                );

                static uint8_t recipe_dir_buf[256];
                GglByteVec recipe_dir_vec = GGL_BYTE_VEC(recipe_dir_buf);
                ret = ggl_byte_vec_append(
                    &recipe_dir_vec,
                    ggl_buffer_from_null_term((char *) args->root_path.data)
                );
                ggl_byte_vec_chain_append(
                    &ret, &recipe_dir_vec, GGL_STR("/packages/recipes/")
                );

                // Write file
                int root_dir_fd = -1;
                ret = ggl_dir_open(
                    recipe_dir_vec.buf, O_PATH, true, &root_dir_fd
                );
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE(
                        "ggdeploymentd",
                        "Failed to open dir when writing cloud recipe."
                    );
                    return;
                }

                int fd = -1;
                ret = ggl_file_openat(
                    root_dir_fd,
                    recipe_name_vec.buf,
                    O_CREAT | O_WRONLY,
                    (mode_t) 0644,
                    &fd
                );
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE(
                        "ggdeploymentd",
                        "Failed to open file at the dir when writing cloud "
                        "recipe."
                    );
                    return;
                }

                ret = ggl_write_exact(fd, recipe_file_content->buf);
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE(
                        "ggdeploymentd", "Write to cloud recipe file failed"
                    );
                    return;
                }

                GGL_LOGD(
                    "ggdeploymentd",
                    "Saved recipe under the name %s",
                    recipe_name_vec.buf.data
                );

                // TODO: Replace with new recipe2unit c script/do not lazy copy
                // paste
                char recipe_path[256] = { 0 };
                strncat(
                    recipe_path,
                    (char *) args->root_path.data,
                    args->root_path.len
                );
                strncat(
                    recipe_path,
                    "/packages/recipes/",
                    strlen("/packages/recipes/")
                );
                strncat(
                    recipe_path,
                    (char *) cloud_component_name->buf.data,
                    cloud_component_name->buf.len
                );
                strncat(recipe_path, "-", strlen("-"));
                strncat(
                    recipe_path,
                    (char *) cloud_component_version->buf.data,
                    cloud_component_version->buf.len
                );
                strncat(recipe_path, ".yaml", strlen(".yaml"));

                char recipe_runner_path[256] = { 0 };
                strncat(
                    recipe_runner_path, args->bin_path, strlen(args->bin_path)
                );
                strncat(
                    recipe_runner_path, "recipe-runner", strlen("recipe-runner")
                );

                char socket_path[256] = { 0 };
                strncat(
                    socket_path,
                    (char *) args->root_path.data,
                    args->root_path.len
                );
                strncat(
                    socket_path, "/gg-ipc.socket", strlen("/gg-ipc.socket")
                );

                char *thing_name = NULL;
                ret = get_thing_name(&thing_name);
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
                    GGL_LOGE(
                        "ggdeploymentd", "Failed to get tes credentials url."
                    );
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
                        "ggdeploymentd",
                        "Run with default posix user is not set."
                    );
                    return;
                }
                bool colon_found = false;
                char *group;
                for (size_t j = 0; j < strlen(posix_user); j++) {
                    if (posix_user[j] == ':') {
                        posix_user[j] = '\0';
                        colon_found = true;
                        group = &posix_user[j + 1];
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
                strncat(
                    artifact_path,
                    (char *) cloud_component_name->buf.data,
                    cloud_component_name->buf.len
                );
                strncat(artifact_path, "/", strlen("/"));
                strncat(
                    artifact_path,
                    (char *) cloud_component_version->buf.data,
                    cloud_component_version->buf.len
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
                        GGL_LOGE(
                            "ggdeploymentd", "Recipe to unit script failed"
                        );
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
                strncat(
                    install_file_path,
                    (char *) cloud_component_name->buf.data,
                    cloud_component_name->buf.len
                );
                strncat(
                    install_file_path,
                    ".script.install",
                    strlen(".script.install")
                );
                int open_ret = open(install_file_path, O_RDONLY | O_CLOEXEC);
                if (open_ret < 0) {
                    GGL_LOGE("ggdeploymentd", "Could not find install file.");
                    return;
                }

                char service_file_path[256] = { 0 };
                strncat(service_file_path, "ggl.", strlen("ggl."));
                strncat(
                    service_file_path,
                    (char *) cloud_component_name->buf.data,
                    cloud_component_name->buf.len
                );
                strncat(service_file_path, ".service", strlen(".service"));
                open_ret = open(service_file_path, O_RDONLY | O_CLOEXEC);
                if (open_ret < 0) {
                    GGL_LOGE("ggdeploymentd", "Could not find service file.");
                    return;
                }

                char dot_slash_install[256] = { 0 };
                strncat(dot_slash_install, "./", strlen("./"));
                strncat(
                    dot_slash_install,
                    install_file_path,
                    strlen(install_file_path)
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
                        "ggdeploymentd",
                        "Error getting current working directory."
                    );
                    return;
                }
                strncat(link_command, cwd, strlen(cwd));
                strncat(link_command, "/", strlen("/"));
                strncat(
                    link_command, service_file_path, strlen(service_file_path)
                );
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
                        "ggdeploymentd",
                        "systemctl enable did not exit normally"
                    );
                    return;
                }
            }
        }
    }

    if (deployment->root_component_versions_to_add.len != 0) {
        GGL_MAP_FOREACH(pair, deployment->root_component_versions_to_add) {
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

        update_current_jobs_deployment(
            deployment->deployment_id, GGL_STR("IN_PROGRESS")
        );

        handle_deployment(deployment, args);

        GGL_LOGD("deployment-handler", "Completed deployment.");

        // TODO: need error details from handle_deployment
        update_current_jobs_deployment(
            deployment->deployment_id, GGL_STR("SUCCEEDED")
        );

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
