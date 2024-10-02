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
#include <ggl/core_bus/gg_config.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/http.h>
#include <ggl/json_decode.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/recipe2unit.h>
#include <ggl/socket.h>
#include <ggl/vector.h>
#include <limits.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_RECIPE_BUF_SIZE 256000

static struct DeploymentConfiguration {
    char data_endpoint[128];
    char cert_path[128];
    char rootca_path[128];
    char pkey_path[128];
    char region[24];
    char port[16];
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
    static uint8_t resp_mem[129] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);
    resp.len -= 1;

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("thingName")), &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("ggdeploymentd", "Failed to get thing name from config.");
        return ret;
    }
    resp.data[resp.len] = '\0';

    *thing_name = (char *) resp.data;
    return GGL_ERR_OK;
}

static GglError get_region(GglByteVec *region) {
    static uint8_t resp_mem[129] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);
    resp.len -= 1;

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.Nucleus-Lite"),
            GGL_STR("configuration"),
            GGL_STR("awsRegion")
        ),
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("ggdeploymentd", "Failed to get region from config.");
        return ret;
    }
    resp.data[resp.len] = '\0';

    ret = ggl_byte_vec_append(region, resp);
    ggl_byte_vec_chain_push(&ret, region, '\0');
    return ret;
}

static GglError get_root_ca_path(char **root_ca_path) {
    static uint8_t resp_mem[129] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);
    resp.len -= 1;

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootCaPath")), &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("ggdeploymentd", "Failed to get rootCaPath from config.");
        return ret;
    }
    resp.data[resp.len] = '\0';

    *root_ca_path = (char *) resp.data;
    return GGL_ERR_OK;
}

static GglError get_tes_cred_url(char **tes_cred_url) {
    static uint8_t resp_mem[129] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);
    resp.len -= 1;

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.Nucleus-Lite"),
            GGL_STR("configuration"),
            GGL_STR("tesCredUrl")
        ),
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("ggdeploymentd", "Failed to get tesCredUrl from config.");
        return ret;
    }
    resp.data[resp.len] = '\0';

    *tes_cred_url = (char *) resp.data;
    return GGL_ERR_OK;
}

static GglError get_posix_user(char **posix_user) {
    static uint8_t resp_mem[129] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);
    resp.len -= 1;

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.Nucleus-Lite"),
            GGL_STR("configuration"),
            GGL_STR("runWithDefault"),
            GGL_STR("posixUser")
        ),
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("ggdeploymentd", "Failed to get posixUser from config.");
        return ret;
    }
    resp.data[resp.len] = '\0';

    *posix_user = (char *) resp.data;
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

static GglError download_s3_artifact(
    GglBuffer root_path,
    GglBuffer component_name,
    GglBuffer component_version,
    GglBuffer region,
    GglBuffer bucket,
    GglBuffer key
) {
    // reverse-find the filename from the key
    GglBuffer file_name = GGL_STR("");
    for (size_t i = key.len; i > 0; --i) {
        if (key.data[i - 1] == '/') {
            file_name = ggl_buffer_substr(key, i, SIZE_MAX);
            break;
        }
    }
    if (file_name.len == 0) {
        file_name = key;
    }

    static char url_for_download[512];
    GglByteVec url_vec = GGL_BYTE_VEC(url_for_download);
    GglError error = GGL_ERR_OK;
    ggl_byte_vec_chain_append(&error, &url_vec, GGL_STR("https://"));
    ggl_byte_vec_chain_append(&error, &url_vec, bucket);
    ggl_byte_vec_chain_append(&error, &url_vec, GGL_STR(".s3."));
    ggl_byte_vec_chain_append(&error, &url_vec, region);
    ggl_byte_vec_chain_append(&error, &url_vec, GGL_STR(".amazonaws.com/"));
    ggl_byte_vec_chain_append(&error, &url_vec, key);
    ggl_byte_vec_chain_append(&error, &url_vec, GGL_STR("\0"));
    if (error != GGL_ERR_OK) {
        return error;
    }

    // TODO: rework into using directory openats (requires ggl-http change)
    static char file_path[512];
    GglByteVec file_vec = GGL_BYTE_VEC(file_path);
    ggl_byte_vec_chain_append(&error, &file_vec, root_path);
    ggl_byte_vec_chain_append(
        &error, &file_vec, GGL_STR("/packages/artifacts/")
    );
    ggl_byte_vec_chain_append(&error, &file_vec, component_name);
    ggl_byte_vec_chain_push(&error, &file_vec, '/');
    ggl_byte_vec_chain_append(&error, &file_vec, component_version);
    ggl_byte_vec_chain_push(&error, &file_vec, '/');
    ggl_byte_vec_chain_append(&error, &file_vec, file_name);
    ggl_byte_vec_chain_push(&error, &file_vec, '\0');
    if (error != GGL_ERR_OK) {
        return error;
    }
    file_vec.buf.len--;
    int fd = -1;
    error
        = ggl_file_open(file_vec.buf, O_WRONLY | O_TRUNC | O_CREAT, 0644, &fd);
    if (error != GGL_ERR_OK) {
        return error;
    }

    FILE *file = fdopen(fd, "wb");
    GGL_DEFER(fclose, file);

    static uint8_t credentials_alloc[1500];
    static GglBuffer tesd = GGL_STR("/aws/ggl/tesd");
    GglObject result;
    GglMap params = { 0 };
    GglBumpAlloc credential_alloc
        = ggl_bump_alloc_init(GGL_BUF(credentials_alloc));

    // TODO: hoist out. Credentials should be good for an hour of s3 requests
    error = ggl_call(
        tesd,
        GGL_STR("request_credentials"),
        params,
        NULL,
        &credential_alloc.alloc,
        &result
    );
    if (error != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }

    GglObject *aws_access_key_id = NULL;
    GglObject *aws_secret_access_key = NULL;
    GglObject *aws_session_token = NULL;

    GglError ret = ggl_map_validate(
        result.map,
        GGL_MAP_SCHEMA(
            { GGL_STR("accessKeyId"), true, GGL_TYPE_BUF, &aws_access_key_id },
            { GGL_STR("secretAccessKey"),
              true,
              GGL_TYPE_BUF,
              &aws_secret_access_key },
            { GGL_STR("sessionToken"), true, GGL_TYPE_BUF, &aws_session_token },
        )
    );
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }

    sigv4_download(
        url_for_download,
        file,
        (SigV4Details) { .access_key_id = aws_access_key_id->buf,
                         .secret_access_key = aws_secret_access_key->buf,
                         .aws_region = region,
                         .aws_service = GGL_STR("s3"),
                         .session_token = aws_session_token->buf }
    );

    return GGL_ERR_OK;
}

static GglError download_artifact(
    GglBuffer root_path,
    GglBuffer component_name,
    GglBuffer component_version,
    GglBuffer region,
    GglObject uri
) {
    if (uri.type != GGL_TYPE_BUF) {
        return GGL_ERR_INVALID;
    }

    size_t i = 0;
    for (; i < uri.buf.len; ++i) {
        if (uri.buf.data[i] == ':') {
            break;
        }
    }
    if (i == uri.buf.len) {
        GGL_LOGE("ggdeploymentd", "Missing artifact URI scheme.");
        return GGL_ERR_PARSE;
    }
    GglBuffer scheme = ggl_buffer_substr(uri.buf, 0, i);
    // skip "://"
    GglBuffer rest = ggl_buffer_substr(uri.buf, i + 3, SIZE_MAX);
    if (rest.len == 0) {
        GGL_LOGE("ggdeploymentd", "Artifact has no URI.");
        return GGL_ERR_PARSE;
    }

    if (ggl_buffer_eq(scheme, GGL_STR("s3"))) {
        for (i = 0; i < rest.len; ++i) {
            if (rest.data[i] == '/') {
                break;
            }
        }
        if (i == rest.len) {
            GGL_LOGE("ggdeploymentd", "Missing s3 key/bucket name.");
            return GGL_ERR_PARSE;
        }
        GglBuffer bucket = ggl_buffer_substr(rest, 0, i);
        GglBuffer key = ggl_buffer_substr(rest, i + 1, SIZE_MAX);
        if (key.len == 0) {
            GGL_LOGE("ggdeploymentd", "s3 key was empty.");
        }
        return download_s3_artifact(
            root_path, component_name, component_version, region, bucket, key
        );
    }

    GGL_LOGE("ggdeploymentd", "Unknown artifact URI scheme.");
    return GGL_ERR_PARSE;
}

static GglError get_recipe_artifacts(
    GglBuffer root_path,
    GglBuffer component_name,
    GglBuffer component_version,
    GglBuffer region,
    GglObject recipe_obj
) {
    if (recipe_obj.type != GGL_TYPE_MAP) {
        return GGL_ERR_INVALID;
    }
    GglObject *cursor = NULL;
    // TODO: use recipe-2-unit recipe parser for manifest selection
    if (!ggl_map_get(recipe_obj.map, GGL_STR("Manifests"), &cursor)) {
        GGL_LOGW("ggdeploymentd", "Manifests is missing");
        return GGL_ERR_OK;
    }
    if (cursor->type != GGL_TYPE_LIST) {
        return GGL_ERR_PARSE;
    }
    if (cursor->list.len == 0) {
        GGL_LOGW("ggdeploymentd", "Manifests is empty");
        return GGL_ERR_OK;
    }
    // FIXME: assumes first artifact is the right one
    if (!ggl_map_get(
            cursor->list.items[0].map, GGL_STR("Artifacts"), &cursor
        )) {
        return GGL_ERR_PARSE;
    }
    if (cursor->type != GGL_TYPE_LIST) {
        return GGL_ERR_PARSE;
    }

    for (size_t i = 0; i < cursor->list.len; ++i) {
        if (cursor->list.items[i].type != GGL_TYPE_MAP) {
            return GGL_ERR_PARSE;
        }
        GglObject *uri_obj = NULL;
        if (!ggl_map_get(cursor->list.items[i].map, GGL_STR("Uri"), &uri_obj)) {
            GGL_LOGW("ggdeploymentd", "Skipping unsupported artifact");
            continue;
        }
        GglError ret = download_artifact(
            root_path, component_name, component_version, region, *uri_obj
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
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

    if (deployment->artifacts_directory_path.len != 0) {
        GglError ret = merge_dir_to(
            deployment->artifacts_directory_path,
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

        GglByteVec region = GGL_BYTE_VEC(config.region);
        ret = get_region(&region);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("ggdeploymentd", "Failed to get region.");
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
            if (!ggl_map_get(
                    json_candidates_response_obj.map,
                    GGL_STR("resolvedComponentVersions"),
                    &resolved_component_versions
                )) {
                GGL_LOGE("ggdeploymentd", "Missing resolvedComponentVersions.");
                return;
            }
            if (resolved_component_versions->type != GGL_TYPE_LIST) {
                GGL_LOGE(
                    "ggdeploymentd",
                    "resolvedComponentVersions response is not a list."
                );
                return;
            }

            GGL_LIST_FOREACH(
                resolved_version, resolved_component_versions->list
            ) {
                if (resolved_version->type != GGL_TYPE_MAP) {
                    GGL_LOGE(
                        "ggdeploymentd", "Resolved version is not of type map."
                    );
                    return;
                }

                GglObject *cloud_component_name;
                GglObject *cloud_component_version;
                GglObject *vendor_guidance;
                GglObject *recipe_file_content;

                ret = ggl_map_validate(
                    resolved_version->map,
                    GGL_MAP_SCHEMA(
                        { GGL_STR("componentName"),
                          true,
                          GGL_TYPE_BUF,
                          &cloud_component_name },
                        { GGL_STR("componentVersion"),
                          true,
                          GGL_TYPE_BUF,
                          &cloud_component_version },
                        { GGL_STR("vendorGuidance"),
                          false,
                          GGL_TYPE_BUF,
                          &vendor_guidance },
                        { GGL_STR("recipe"),
                          true,
                          GGL_TYPE_BUF,
                          &recipe_file_content },
                    )
                );
                if (ret != GGL_ERR_OK) {
                    return;
                }

                if (vendor_guidance != NULL) {
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

                if (recipe_file_content->buf.len == 0) {
                    GGL_LOGE("ggdeploymentd", "Recipe is empty.");
                }

                ggl_base64_decode_in_place(&recipe_file_content->buf);
                recipe_file_content->buf.data[recipe_file_content->buf.len]
                    = '\0';

                GGL_LOGD(
                    "ggdeploymentd",
                    "Decoded recipe data as: %.*s",
                    (int) recipe_file_content->buf.len,
                    recipe_file_content->buf.data
                );

                static uint8_t recipe_name_buf[PATH_MAX];
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

                static uint8_t recipe_dir_buf[PATH_MAX];
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
                    O_CREAT | O_WRONLY | O_TRUNC,
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

                static uint8_t recipe_obj_buf[4096];
                GglObject recipe_obj = GGL_OBJ_NULL();
                GglBumpAlloc alloc
                    = ggl_bump_alloc_init(GGL_BUF(recipe_obj_buf));
                ret = ggl_json_decode_destructive(
                    recipe_file_content->buf, &alloc.alloc, &recipe_obj
                );
                if (ret == GGL_ERR_OK) {
                    ret = get_recipe_artifacts(
                        args->root_path,
                        cloud_component_name->buf,
                        cloud_component_version->buf,
                        ggl_buffer_from_null_term(config.region),
                        recipe_obj
                    );
                }
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE("ggdeploymentd", "Failed to validate recipe");
                    return;
                }

                // TODO: Replace with new recipe2unit c script/do not lazy copy
                // paste
                char recipe_path[PATH_MAX] = { 0 };
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

                char recipe_runner_path[PATH_MAX] = { 0 };
                strncat(
                    recipe_runner_path, args->bin_path, strlen(args->bin_path)
                );
                strncat(
                    recipe_runner_path, "recipe-runner", strlen("recipe-runner")
                );

                char socket_path[PATH_MAX] = { 0 };
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

                char artifact_path[PATH_MAX] = { 0 };
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

                Recipe2UnitArgs recipe2unit_args
                    = { .user = posix_user, .group = group };
                memcpy(
                    recipe2unit_args.recipe_path,
                    recipe_path,
                    strnlen(recipe_path, PATH_MAX)
                );
                memcpy(
                    recipe2unit_args.recipe_runner_path,
                    recipe_runner_path,
                    strnlen(recipe_runner_path, PATH_MAX)
                );
                memcpy(
                    recipe2unit_args.root_dir,
                    args->root_path.data,
                    args->root_path.len
                );

                GglObject recipe_buff_obj;
                GglObject *component_name;
                static uint8_t big_buffer_for_bump[MAX_RECIPE_BUF_SIZE];
                GglBumpAlloc bump_alloc
                    = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

                GglError err = convert_to_unit(
                    &recipe2unit_args,
                    &bump_alloc.alloc,
                    &recipe_buff_obj,
                    &component_name
                );

                if (err != GGL_ERR_OK) {
                    return;
                }

                GglObject *intermediate_obj;
                GglObject *default_config_obj;

                if (ggl_map_get(
                        recipe_buff_obj.map,
                        GGL_STR("componentconfiguration"),
                        &intermediate_obj
                    )) {
                    if (intermediate_obj->type != GGL_TYPE_MAP) {
                        GGL_LOGE(
                            "Deployment Handler",
                            "ComponentConfiguration is not a map type"
                        );
                        return;
                    }

                    if (ggl_map_get(
                            intermediate_obj->map,
                            GGL_STR("defaultconfiguration"),
                            &default_config_obj
                        )) {
                        ret = ggl_gg_config_write(
                            GGL_BUF_LIST(
                                GGL_STR("services"), component_name->buf
                            ),
                            *default_config_obj,
                            0
                        );

                        if (ret != GGL_ERR_OK) {
                            GGL_LOGE(
                                "Deployment Handler",
                                "Failed to send default config to ggconfigd."
                            );
                            return;
                        }
                    } else {
                        GGL_LOGI(
                            "Deployment Handler",
                            "DefaultConfiguration not found in the recipe."
                        );
                    }
                } else {
                    GGL_LOGI(
                        "Deployment Handler",
                        "ComponentConfiguration not found in the recipe"
                    );
                }

                // TODO: add install file processing logic here.

                char service_file_path[PATH_MAX] = { 0 };
                strncat(service_file_path, "ggl.", strlen("ggl."));
                strncat(
                    service_file_path,
                    (char *) cloud_component_name->buf.data,
                    cloud_component_name->buf.len
                );
                strncat(service_file_path, ".service", strlen(".service"));

                char link_command[PATH_MAX] = { 0 };
                strncat(
                    link_command,
                    "sudo systemctl link ",
                    strlen("sudo systemctl link ")
                );
                strncat(
                    link_command,
                    (const char *) args->root_path.data,
                    args->root_path.len
                );
                strncat(link_command, "/", strlen("/"));
                strncat(
                    link_command, service_file_path, strlen(service_file_path)
                );
                // NOLINTNEXTLINE(concurrency-mt-unsafe)
                int system_ret = system(link_command);
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

                char start_command[PATH_MAX] = { 0 };
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

                char enable_command[PATH_MAX] = { 0 };
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

            char recipe_path[PATH_MAX] = { 0 };
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

            char recipe_runner_path[PATH_MAX] = { 0 };
            strncat(recipe_runner_path, args->bin_path, strlen(args->bin_path));
            strncat(
                recipe_runner_path, "recipe-runner", strlen("recipe-runner")
            );

            char socket_path[PATH_MAX] = { 0 };
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

            GglByteVec region = GGL_BYTE_VEC(config.region);
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

            char artifact_path[PATH_MAX] = { 0 };
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

            Recipe2UnitArgs recipe2unit_args
                = { .group = group, .user = posix_user };
            GGL_LOGI("Deployment Handler", "Recipepath %s", recipe_path);
            memcpy(
                recipe2unit_args.recipe_path,
                recipe_path,
                strnlen(recipe_path, PATH_MAX)
            );
            memcpy(
                recipe2unit_args.recipe_runner_path,
                recipe_runner_path,
                strnlen(recipe_runner_path, PATH_MAX)
            );
            memcpy(
                recipe2unit_args.root_dir,
                args->root_path.data,
                args->root_path.len
            );

            GglObject recipe_buff_obj;
            GglObject *component_name;
            static uint8_t big_buffer_for_bump[MAX_RECIPE_BUF_SIZE];
            GglBumpAlloc bump_alloc
                = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

            GglError err = convert_to_unit(
                &recipe2unit_args,
                &bump_alloc.alloc,
                &recipe_buff_obj,
                &component_name
            );

            if (err != GGL_ERR_OK) {
                return;
            }

            GglObject *intermediate_obj;
            GglObject *default_config_obj;

            if (ggl_map_get(
                    recipe_buff_obj.map,
                    GGL_STR("componentconfiguration"),
                    &intermediate_obj
                )) {
                if (intermediate_obj->type != GGL_TYPE_MAP) {
                    GGL_LOGE(
                        "Deployment Handler",
                        "ComponentConfiguration is not a map type"
                    );
                    return;
                }

                if (ggl_map_get(
                        intermediate_obj->map,
                        GGL_STR("defaultconfiguration"),
                        &default_config_obj
                    )) {
                    ret = ggl_gg_config_write(
                        GGL_BUF_LIST(GGL_STR("services"), component_name->buf),
                        *default_config_obj,
                        0
                    );

                    if (ret != GGL_ERR_OK) {
                        GGL_LOGE(
                            "Deployment Handler",
                            "Failed to send default config to ggconfigd."
                        );
                        return;
                    }
                } else {
                    GGL_LOGI(
                        "Deployment Handler",
                        "DefaultConfiguration not found in the recipe."
                    );
                }
            } else {
                GGL_LOGI(
                    "Deployment Handler",
                    "ComponentConfiguration not found in the recipe"
                );
            }

            // TODO: add install file processing logic here.

            char service_file_path[PATH_MAX] = { 0 };
            strncat(service_file_path, "ggl.com.example.", strlen("ggl.com.example."));
            strncat(service_file_path, (char *) pair->key.data, pair->key.len);
            strncat(service_file_path, ".service", strlen(".service"));

            char link_command[PATH_MAX] = { 0 };
            strncat(
                link_command,
                "sudo systemctl link ",
                strlen("sudo systemctl link ")
            );
            strncat(
                link_command,
                (const char *) args->root_path.data,
                args->root_path.len
            );
            strncat(link_command, "/", strlen("/"));
            strncat(link_command, service_file_path, strlen(service_file_path));
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            int system_ret = system(link_command);
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

            char start_command[PATH_MAX] = { 0 };
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

            char enable_command[PATH_MAX] = { 0 };
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
