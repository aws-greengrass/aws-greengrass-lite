// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "deployment_handler.h"
#include "component_manager.h"
#include "deployment_model.h"
#include "deployment_queue.h"
#include "iot_jobs_listener.h"
#include <sys/types.h>
#include <assert.h>
#include <fcntl.h>
#include <ggl/alloc.h>
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
#include <ggl/recipe.h>
#include <ggl/recipe2unit.h>
#include <ggl/semver.h>
#include <ggl/socket.h>
#include <ggl/uri.h>
#include <ggl/vector.h>
#include <ggl/zip.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_RECIPE_BUF_SIZE 256000
#define MAX_DECODE_BUF_LEN 4096

static struct DeploymentConfiguration {
    char data_endpoint[128];
    char cert_path[128];
    char rootca_path[128];
    char pkey_path[128];
    char region[24];
    char port[16];
} config;

typedef struct TesCredentials {
    GglBuffer aws_region;
    GglBuffer access_key_id;
    GglBuffer secret_access_key;
    GglBuffer session_token;
} TesCredentials;

static SigV4Details sigv4_from_tes(
    TesCredentials credentials, GglBuffer aws_service
) {
    return (SigV4Details) { .aws_region = credentials.aws_region,
                            .aws_service = aws_service,
                            .access_key_id = credentials.access_key_id,
                            .secret_access_key = credentials.secret_access_key,
                            .session_token = credentials.session_token };
}

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
        GGL_LOGW("Failed to get thing name from config.");
        return ret;
    }
    resp.data[resp.len] = '\0';

    *thing_name = (char *) resp.data;
    return GGL_ERR_OK;
}

static GglError get_region(GglByteVec *region) {
    static uint8_t resp_mem[128] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

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
        GGL_LOGW("Failed to get region from config.");
        return ret;
    }

    ggl_byte_vec_chain_append(&ret, region, resp);
    ggl_byte_vec_chain_push(&ret, region, '\0');
    if (ret == GGL_ERR_OK) {
        region->buf.len--;
    }
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
        GGL_LOGW("Failed to get rootCaPath from config.");
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
        GGL_LOGW("Failed to get tesCredUrl from config.");
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
        GGL_LOGW("Failed to get posixUser from config.");
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
        GGL_LOGW("Failed to get dataplane endpoint from config.");
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE("Configuration dataplane endpoint is not a string.");
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
        GGL_LOGW("Failed to get dataplane port from config.");
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE("Configuration dataplane port is not a string.");
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
        GGL_LOGW("Failed to get private key path from config.");
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE("Configuration private key path is not a string.");
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
        GGL_LOGW("Failed to get certificate path from config.");
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE("Configuration certificate path is not a string.");
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
        GGL_LOGW("Failed to get rootca path from config.");
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE("Configuration rootca path is not a string.");
        return GGL_ERR_INVALID;
    }

    ggl_byte_vec_chain_append(&ret, rootca_path, resp.buf);
    ggl_byte_vec_chain_push(&ret, rootca_path, '\0');
    return ret;
}

static GglError get_tes_credentials(TesCredentials *tes_creds) {
    GglObject *aws_access_key_id = NULL;
    GglObject *aws_secret_access_key = NULL;
    GglObject *aws_session_token = NULL;

    static uint8_t credentials_alloc[1500];
    static GglBuffer tesd = GGL_STR("/aws/ggl/tesd");
    GglObject result;
    GglMap params = { 0 };
    GglBumpAlloc credential_alloc
        = ggl_bump_alloc_init(GGL_BUF(credentials_alloc));

    GglError ret = ggl_call(
        tesd,
        GGL_STR("request_credentials"),
        params,
        NULL,
        &credential_alloc.alloc,
        &result
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get TES credentials.");
        return GGL_ERR_FAILURE;
    }

    ret = ggl_map_validate(
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
        GGL_LOGE("Failed to validate TES credentials."

        );
        return GGL_ERR_FAILURE;
    }
    tes_creds->access_key_id = aws_access_key_id->buf;
    tes_creds->secret_access_key = aws_secret_access_key->buf;
    tes_creds->session_token = aws_session_token->buf;
    return GGL_ERR_OK;
}

static GglError download_s3_artifact(
    GglBuffer scratch_buffer,
    GglUriInfo uri_info,
    TesCredentials credentials,
    int artifact_fd
) {
    GglByteVec url_vec = ggl_byte_vec_init(scratch_buffer);
    GglError error = GGL_ERR_OK;
    ggl_byte_vec_chain_append(&error, &url_vec, GGL_STR("https://"));
    ggl_byte_vec_chain_append(&error, &url_vec, uri_info.host);
    ggl_byte_vec_chain_append(&error, &url_vec, GGL_STR(".s3."));
    ggl_byte_vec_chain_append(&error, &url_vec, credentials.aws_region);
    ggl_byte_vec_chain_append(&error, &url_vec, GGL_STR(".amazonaws.com/"));
    ggl_byte_vec_chain_append(&error, &url_vec, uri_info.path);
    ggl_byte_vec_chain_push(&error, &url_vec, '\0');
    if (error != GGL_ERR_OK) {
        return error;
    }

    return sigv4_download(
        (const char *) url_vec.buf.data,
        artifact_fd,
        sigv4_from_tes(credentials, GGL_STR("s3"))
    );
}

static GglError download_greengrass_artifact(
    GglBuffer scratch_buffer,
    GglBuffer component_arn,
    GglBuffer uri_path,
    CertificateDetails credentials,
    int artifact_fd
) {
    // For holding a presigned S3 URL
    static uint8_t response_data[2000];

    GglError err = GGL_ERR_OK;
    // https://docs.aws.amazon.com/greengrass/v2/APIReference/API_GetComponentVersionArtifact.html
    GglByteVec uri_path_vec = ggl_byte_vec_init(scratch_buffer);
    ggl_byte_vec_chain_append(
        &err, &uri_path_vec, GGL_STR("greengrass/v2/components/")
    );
    ggl_byte_vec_chain_append(&err, &uri_path_vec, component_arn);
    ggl_byte_vec_chain_append(&err, &uri_path_vec, GGL_STR("/artifacts/"));
    ggl_byte_vec_chain_append(&err, &uri_path_vec, uri_path);
    if (err != GGL_ERR_OK) {
        return err;
    }

    GGL_LOGI("Getting presigned S3 URL");
    GglBuffer response_buffer = GGL_BUF(response_data);
    err = gg_dataplane_call(
        ggl_buffer_from_null_term(config.data_endpoint),
        ggl_buffer_from_null_term(config.port),
        uri_path_vec.buf,
        credentials,
        NULL,
        &response_buffer
    );

    if (err != GGL_ERR_OK) {
        return err;
    }

    // reusing scratch buffer for JSON decoding
    GglBumpAlloc json_bump = ggl_bump_alloc_init(scratch_buffer);
    GglObject response_obj = GGL_OBJ_NULL();
    err = ggl_json_decode_destructive(
        response_buffer, &json_bump.alloc, &response_obj
    );
    if (err != GGL_ERR_OK) {
        return err;
    }
    if (response_obj.type != GGL_TYPE_MAP) {
        return GGL_ERR_PARSE;
    }
    GglObject *presigned_url = NULL;
    err = ggl_map_validate(
        response_obj.map,
        GGL_MAP_SCHEMA(
            { GGL_STR("preSignedUrl"), true, GGL_TYPE_BUF, &presigned_url }
        )
    );
    if (err != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }

    // Should be OK to null-terminate this buffer;
    // it's in the middle of a JSON blob.
    presigned_url->buf.data[presigned_url->buf.len] = '\0';

    GGL_LOGI("Getting presigned S3 URL artifact");

    return generic_download(
        (const char *) (presigned_url->buf.data), artifact_fd
    );
}

static GglError find_artifacts_list(
    GglMap recipe, GglList *platform_artifacts
) {
    GglObject *cursor = NULL;
    // TODO: use recipe-2-unit recipe parser for manifest selection
    if (!ggl_map_get(recipe, GGL_STR("Manifests"), &cursor)) {
        GGL_LOGW("Manifests is missing");
        return GGL_ERR_OK;
    }
    if (cursor->type != GGL_TYPE_LIST) {
        return GGL_ERR_PARSE;
    }
    if (cursor->list.len == 0) {
        GGL_LOGW("Manifests is empty");
        return GGL_ERR_OK;
    }
    // FIXME: assumes first manifest is the right one
    if (!ggl_map_get(
            cursor->list.items[0].map, GGL_STR("Artifacts"), &cursor
        )) {
        return GGL_ERR_PARSE;
    }
    if (cursor->type != GGL_TYPE_LIST) {
        return GGL_ERR_PARSE;
    }
    *platform_artifacts = cursor->list;
    return GGL_ERR_OK;
}

// Get the unarchive type: NONE or ZIP
static GglError get_artifact_unarchive_type(
    GglBuffer unarchive_buf, bool *needs_unarchive
) {
    if (ggl_buffer_eq(unarchive_buf, GGL_STR("NONE"))) {
        *needs_unarchive = false;
    } else if (ggl_buffer_eq(unarchive_buf, GGL_STR("ZIP"))) {
        *needs_unarchive = true;
    } else {
        GGL_LOGE("Unknown archive type");
        return GGL_ERR_UNSUPPORTED;
    }
    return GGL_ERR_OK;
}

static GglError unarchive_artifact(
    int component_store_fd,
    GglBuffer zip_file,
    mode_t mode,
    int component_archive_store_fd
) {
    GglBuffer destination_dir = zip_file;
    if (ggl_buffer_has_suffix(zip_file, GGL_STR(".zip"))) {
        destination_dir = ggl_buffer_substr(
            zip_file, 0, zip_file.len - (sizeof(".zip") - 1U)
        );
    }

    GGL_LOGD("Unarchive %.*s", (int) zip_file.len, zip_file.data);

    int output_dir_fd;
    GglError err = ggl_dir_openat(
        component_archive_store_fd,
        destination_dir,
        O_PATH,
        true,
        &output_dir_fd
    );
    if (err != GGL_ERR_OK) {
        return err;
    }

    // Unarchive the zip
    return ggl_zip_unarchive(component_store_fd, zip_file, output_dir_fd, mode);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GglError get_recipe_artifacts(
    GglBuffer component_arn,
    TesCredentials tes_creds,
    CertificateDetails iot_creds,
    GglMap recipe,
    int component_store_fd,
    int component_archive_store_fd
) {
    GglList artifacts = { 0 };
    GglError error = find_artifacts_list(recipe, &artifacts);
    if (error != GGL_ERR_OK) {
        return error;
    }

    for (size_t i = 0; i < artifacts.len; ++i) {
        uint8_t decode_buffer[MAX_DECODE_BUF_LEN];
        if (artifacts.items[i].type != GGL_TYPE_MAP) {
            return GGL_ERR_PARSE;
        }
        GglObject *uri_obj = NULL;
        GglObject *unarchive_obj = NULL;

        GglError err = ggl_map_validate(
            artifacts.items[i].map,
            GGL_MAP_SCHEMA(
                { GGL_STR("Uri"), true, GGL_TYPE_BUF, &uri_obj },
                { GGL_STR("Unarchive"), false, GGL_TYPE_BUF, &unarchive_obj }
            )
        );
        if (err != GGL_ERR_OK) {
            return GGL_ERR_PARSE;
        }

        GglUriInfo info = { 0 };
        {
            GglBumpAlloc alloc = ggl_bump_alloc_init(GGL_BUF(decode_buffer));
            err = gg_uri_parse(&alloc.alloc, uri_obj->buf, &info);

            if (err != GGL_ERR_OK) {
                return err;
            }
        }

        bool needs_unarchive = false;
        if (unarchive_obj != NULL) {
            err = get_artifact_unarchive_type(
                unarchive_obj->buf, &needs_unarchive
            );
            if (err != GGL_ERR_OK) {
                return err;
            }
        }

        // TODO: set permissions from recipe
        mode_t mode = 0755;
        int artifact_fd = -1;
        err = ggl_file_openat(
            component_store_fd,
            info.file,
            O_CREAT | O_WRONLY | O_TRUNC,
            needs_unarchive ? 0644 : mode,
            &artifact_fd
        );
        GGL_DEFER(ggl_close, artifact_fd);

        if (err != GGL_ERR_OK) {
            GGL_LOGE("Failed to create artifact file for write.");
            return err;
        }

        if (ggl_buffer_eq(GGL_STR("s3"), info.scheme)) {
            err = download_s3_artifact(
                GGL_BUF(decode_buffer), info, tes_creds, artifact_fd
            );
        } else if (ggl_buffer_eq(GGL_STR("greengrass"), info.scheme)) {
            err = download_greengrass_artifact(
                GGL_BUF(decode_buffer),
                component_arn,
                info.path,
                iot_creds,
                artifact_fd
            );
        } else {
            GGL_LOGE("Unknown artifact URI scheme");
            err = GGL_ERR_PARSE;
        }

        if (err != GGL_ERR_OK) {
            return err;
        }

        // Unarchive the ZIP file if needed
        if (needs_unarchive) {
            err = unarchive_artifact(
                component_store_fd, info.file, mode, component_archive_store_fd
            );
            if (err != GGL_ERR_OK) {
                return err;
            }
        }

        err = ggl_fsync(artifact_fd);
        if (err != GGL_ERR_OK) {
            return err;
        }

        GglError close_error = ggl_close(artifact_fd);
        if (close_error != GGL_ERR_OK) {
            return close_error;
        }
    }
    return GGL_ERR_OK;
}

static GglError get_device_thing_groups(GglBuffer *response) {
    GglByteVec data_endpoint = GGL_BYTE_VEC(config.data_endpoint);
    GglError ret = get_data_endpoint(&data_endpoint);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get dataplane endpoint.");
        return ret;
    }

    GglByteVec region = GGL_BYTE_VEC(config.region);
    ret = get_region(&region);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get region.");
        return ret;
    }

    GglByteVec port = GGL_BYTE_VEC(config.port);
    ret = get_data_port(&port);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get dataplane port.");
        return ret;
    }

    GglByteVec pkey_path = GGL_BYTE_VEC(config.pkey_path);
    ret = get_private_key_path(&pkey_path);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get private key path.");
        return ret;
    }

    GglByteVec cert_path = GGL_BYTE_VEC(config.cert_path);
    ret = get_cert_path(&cert_path);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get certificate path.");
        return ret;
    }

    GglByteVec rootca_path = GGL_BYTE_VEC(config.rootca_path);
    ret = get_rootca_path(&rootca_path);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get certificate path.");
        return ret;
    }

    CertificateDetails cert_details
        = { .gghttplib_cert_path = config.cert_path,
            .gghttplib_root_ca_path = config.rootca_path,
            .gghttplib_p_key_path = config.pkey_path };

    char *thing_name = NULL;
    ret = get_thing_name(&thing_name);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get thing name.");
        return ret;
    }

    static uint8_t uri_path_buf[PATH_MAX];
    GglByteVec uri_path_vec = GGL_BYTE_VEC(uri_path_buf);
    ret = ggl_byte_vec_append(
        &uri_path_vec, GGL_STR("greengrass/v2/coreDevices/")
    );
    ggl_byte_vec_chain_append(
        &ret, &uri_path_vec, ggl_buffer_from_null_term(thing_name)
    );
    ggl_byte_vec_chain_append(&ret, &uri_path_vec, GGL_STR("/thingGroups"));
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to create thing groups call uri.");
        return ret;
    }

    ret = gg_dataplane_call(
        data_endpoint.buf,
        port.buf,
        uri_path_vec.buf,
        cert_details,
        NULL,
        response
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "The listThingGroupsForCoreDevice call failed with response %.*s.",
            (int) response->len,
            response->data
        );
        return ret;
    }

    GGL_LOGD(
        "Received response from thingGroups dataplane call: %.*s",
        (int) response->len,
        response->data
    );

    return GGL_ERR_OK;
}

static GglError generate_resolve_component_candidates_body(
    GglBuffer component_name,
    GglBuffer component_requirements,
    GglByteVec *body_vec
) {
    GglError byte_vec_ret = GGL_ERR_OK;
    ggl_byte_vec_chain_append(
        &byte_vec_ret, body_vec, GGL_STR("{\"componentCandidates\": [")
    );

    ggl_byte_vec_chain_append(
        &byte_vec_ret, body_vec, GGL_STR("{\"componentName\": \"")
    );
    ggl_byte_vec_chain_append(&byte_vec_ret, body_vec, component_name);
    ggl_byte_vec_chain_append(
        &byte_vec_ret,
        body_vec,
        GGL_STR("\",\"versionRequirements\": {\"requirements\": \"")
    );
    ggl_byte_vec_chain_append(&byte_vec_ret, body_vec, component_requirements);
    ggl_byte_vec_chain_append(&byte_vec_ret, body_vec, GGL_STR("\"}}"));

    // TODO: Include architecture requirements if any
    ggl_byte_vec_chain_append(
        &byte_vec_ret,
        body_vec,
        GGL_STR("],\"platform\": { \"attributes\": { \"os\" : \"linux\" "
                "},\"name\": \"linux\"}}")
    );
    ggl_byte_vec_chain_push(&byte_vec_ret, body_vec, '\0');

    GGL_LOGD("Body for call: %s", body_vec->buf.data);

    return GGL_ERR_OK;
}

static GglError resolve_component_with_cloud(
    GglBuffer component_name,
    GglBuffer version_requirements,
    GglBuffer *response
) {
    static char resolve_candidates_body_buf[2048];
    GglByteVec body_vec = GGL_BYTE_VEC(resolve_candidates_body_buf);
    GglError ret = generate_resolve_component_candidates_body(
        component_name, version_requirements, &body_vec
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to generate body for resolveComponentCandidates call");
        return ret;
    }

    GglByteVec data_endpoint = GGL_BYTE_VEC(config.data_endpoint);
    ret = get_data_endpoint(&data_endpoint);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get dataplane endpoint.");
        return ret;
    }

    GglByteVec region = GGL_BYTE_VEC(config.region);
    ret = get_region(&region);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get region.");
        return ret;
    }

    GglByteVec port = GGL_BYTE_VEC(config.port);
    ret = get_data_port(&port);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get dataplane port.");
        return ret;
    }

    GglByteVec pkey_path = GGL_BYTE_VEC(config.pkey_path);
    ret = get_private_key_path(&pkey_path);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get private key path.");
        return ret;
    }

    GglByteVec cert_path = GGL_BYTE_VEC(config.cert_path);
    ret = get_cert_path(&cert_path);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get certificate path.");
        return ret;
    }

    GglByteVec rootca_path = GGL_BYTE_VEC(config.rootca_path);
    ret = get_rootca_path(&rootca_path);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get certificate path.");
        return ret;
    }

    CertificateDetails cert_details
        = { .gghttplib_cert_path = config.cert_path,
            .gghttplib_root_ca_path = config.rootca_path,
            .gghttplib_p_key_path = config.pkey_path };

    ret = gg_dataplane_call(
        data_endpoint.buf,
        port.buf,
        GGL_STR("greengrass/v2/resolveComponentCandidates"),
        cert_details,
        resolve_candidates_body_buf,
        response
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Cloud resolution for the component failed with response %.*s.",
            (int) response->len,
            response->data
        );
        return ret;
    }

    GGL_LOGD(
        "Received response from resolveComponentCandidates: %.*s",
        (int) response->len,
        response->data
    );

    return GGL_ERR_OK;
}

static GglError parse_dataplane_response_and_save_recipe(
    GglBuffer dataplane_response,
    GglDeploymentHandlerThreadArgs *args,
    GglBuffer *cloud_version
) {
    GglObject json_candidates_response_obj;
    // TODO: Figure out a better size. This response can be big.
    uint8_t candidates_response_mem[100 * sizeof(GglObject)];
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(candidates_response_mem));
    GglError ret = ggl_json_decode_destructive(
        dataplane_response, &balloc.alloc, &json_candidates_response_obj
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error when parsing resolveComponentCandidates response to "
                 "json.");
        return ret;
    }

    if (json_candidates_response_obj.type != GGL_TYPE_MAP) {
        GGL_LOGE("resolveComponentCandidates response did not parse into a "
                 "map.");
        return ret;
    }

    GglObject *resolved_component_versions;
    if (!ggl_map_get(
            json_candidates_response_obj.map,
            GGL_STR("resolvedComponentVersions"),
            &resolved_component_versions
        )) {
        GGL_LOGE("Missing resolvedComponentVersions.");
        return ret;
    }
    if (resolved_component_versions->type != GGL_TYPE_LIST) {
        GGL_LOGE("resolvedComponentVersions response is not a list.");
        return ret;
    }

    bool first_component = true;
    GGL_LIST_FOREACH(resolved_version, resolved_component_versions->list) {
        if (!first_component) {
            GGL_LOGE(
                "resolveComponentCandidates returned information for more than "
                "one component."
            );
            return GGL_ERR_INVALID;
        }
        first_component = false;

        if (resolved_version->type != GGL_TYPE_MAP) {
            GGL_LOGE("Resolved version is not of type map.");
            return ret;
        }

        GglObject *cloud_component_arn;
        GglObject *cloud_component_name;
        GglObject *cloud_component_version;
        GglObject *vendor_guidance;
        GglObject *recipe_file_content;

        ret = ggl_map_validate(
            resolved_version->map,
            GGL_MAP_SCHEMA(
                { GGL_STR("arn"), true, GGL_TYPE_BUF, &cloud_component_arn },
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
                { GGL_STR("recipe"), true, GGL_TYPE_BUF, &recipe_file_content },
            )
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        *cloud_version = cloud_component_version->buf;

        if (vendor_guidance != NULL) {
            if (ggl_buffer_eq(vendor_guidance->buf, GGL_STR("DISCONTINUED"))) {
                GGL_LOGW("The component version has been discontinued by "
                         "its "
                         "publisher. You can deploy this component version, "
                         "but "
                         "we recommend that you use a different version of "
                         "this "
                         "component");
            }
        }

        if (recipe_file_content->buf.len == 0) {
            GGL_LOGE("Recipe is empty.");
        }

        ggl_base64_decode_in_place(&recipe_file_content->buf);
        recipe_file_content->buf.data[recipe_file_content->buf.len] = '\0';

        GGL_LOGD(
            "Decoded recipe data as: %.*s",
            (int) recipe_file_content->buf.len,
            recipe_file_content->buf.data
        );

        static uint8_t recipe_name_buf[PATH_MAX];
        GglByteVec recipe_name_vec = GGL_BYTE_VEC(recipe_name_buf);
        ret = ggl_byte_vec_append(&recipe_name_vec, cloud_component_name->buf);
        ggl_byte_vec_chain_append(&ret, &recipe_name_vec, GGL_STR("-"));
        ggl_byte_vec_chain_append(
            &ret, &recipe_name_vec, cloud_component_version->buf
        );
        // TODO: Actual support for .json files. We're writing a .json
        // to a .yaml and relying on yaml being an almost-superset of
        // json.
        ggl_byte_vec_chain_append(&ret, &recipe_name_vec, GGL_STR(".yaml"));
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to create recipe file name.");
            return ret;
        }

        static uint8_t recipe_dir_buf[PATH_MAX];
        GglByteVec recipe_dir_vec = GGL_BYTE_VEC(recipe_dir_buf);
        ret = ggl_byte_vec_append(
            &recipe_dir_vec,
            ggl_buffer_from_null_term((char *) args->root_path.data)
        );
        ggl_byte_vec_chain_append(
            &ret, &recipe_dir_vec, GGL_STR("/packages/recipes/")
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to create recipe directory name.");
            return ret;
        }

        // Write file
        int root_dir_fd = -1;
        ret = ggl_dir_open(recipe_dir_vec.buf, O_PATH, true, &root_dir_fd);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to open dir when writing cloud recipe.");
            return ret;
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
            GGL_LOGE("Failed to open file at the dir when writing cloud "
                     "recipe.");
            return ret;
        }

        ret = ggl_write_exact(fd, recipe_file_content->buf);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Write to cloud recipe file failed");
            return ret;
        }

        GGL_LOGD("Saved recipe under the name %s", recipe_name_vec.buf.data);

        ret = ggl_gg_config_write(
            GGL_BUF_LIST(GGL_STR("services"), cloud_component_name->buf, ),
            GGL_OBJ_MAP({ GGL_STR("arn"), *cloud_component_arn }),
            1
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Write of arn to config failed");
            return ret;
        }
    }

    return GGL_ERR_OK;
}

static GglError resolve_dependencies(
    GglMap root_components,
    GglDeploymentHandlerThreadArgs *args,
    GglAlloc *alloc,
    GglKVVec *resolved_components_kv_vec
) {
    assert(root_components.len != 0);

    GglError ret;

    // TODO: Decide on size
    GglKVVec components_to_resolve = GGL_KV_VEC((GglKV[64]) { 0 });

    static uint8_t version_requirements_mem[2048] = { 0 };
    GglBumpAlloc version_requirements_balloc
        = ggl_bump_alloc_init(GGL_BUF(version_requirements_mem));

    // Root components from current deployment
    // TODO: Add current deployment's thing group to components map to config
    GGL_MAP_FOREACH(pair, root_components) {
        if (pair->val.type != GGL_TYPE_MAP) {
            GGL_LOGE("Incorrect formatting for cloud deployment components "
                     "field.");
            return GGL_ERR_INVALID;
        }

        GglObject *val;
        GglBuffer component_version = { 0 };
        if (ggl_map_get(pair->val.map, GGL_STR("version"), &val)) {
            if (val->type != GGL_TYPE_BUF) {
                GGL_LOGE("Received invalid argument.");
                return GGL_ERR_INVALID;
            }
            component_version = val->buf;
        }

        ret = ggl_kv_vec_push(
            &components_to_resolve,
            (GglKV) { pair->key, GGL_OBJ(component_version) }
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    // Get list of thing groups
    static uint8_t list_thing_groups_response_buf[1024] = { 0 };
    GglBuffer list_thing_groups_response
        = GGL_BUF(list_thing_groups_response_buf);

    ret = get_device_thing_groups(&list_thing_groups_response);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_MAP_FOREACH(pair, components_to_resolve.map) {
        // We assume that we have not resolved a component yet if we are finding
        // it in this map.

        GglBuffer resolved_version = { .len = NAME_MAX };
        bool found_local_candidate = resolve_component_version(
            pair->key, pair->val.buf, &resolved_version
        );

        if (!found_local_candidate) {
            // Resolve with cloud and download recipe
            static uint8_t resolve_component_candidates_response_buf[16384]
                = { 0 };
            GglBuffer resolve_component_candidates_response
                = GGL_BUF(resolve_component_candidates_response_buf);

            ret = resolve_component_with_cloud(
                pair->key, pair->val.buf, &resolve_component_candidates_response
            );
            if (ret != GGL_ERR_OK) {
                return ret;
            }

            bool is_empty_response = ggl_buffer_eq(
                resolve_component_candidates_response, GGL_STR("{}")
            );

            if (is_empty_response) {
                GGL_LOGI(
                    "Cloud version resolution failed for component %.*s.",
                    (int) pair->key.len,
                    pair->val.buf.data
                );
                return GGL_ERR_FAILURE;
            }

            ret = parse_dataplane_response_and_save_recipe(
                resolve_component_candidates_response, args, &resolved_version
            );
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        }

        // Add resolved component to list of resolved components
        GglBuffer val_buf;
        ret = ggl_buf_clone(resolved_version, alloc, &val_buf);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        ret = ggl_kv_vec_push(
            resolved_components_kv_vec, (GglKV) { pair->key, GGL_OBJ(val_buf) }
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Error while adding component to list of resolved component"
            );
            return ret;
        }

        // Find dependencies from recipe and add them to the list of components
        // to resolve. If the dependency is for a component that is already
        // resolved, verify that new requirements are satisfied and fail
        // deployment if not.

        // Get actual recipe read
        GglObject recipe_obj;
        static uint8_t recipe_mem[8192] = { 0 };
        GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(recipe_mem));
        ret = ggl_recipe_get_from_file(
            args->root_path_fd,
            pair->key,
            resolved_version,
            &balloc.alloc,
            &recipe_obj
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        GglObject *component_dependencies = NULL;

        if (recipe_obj.type != GGL_TYPE_MAP) {
            GGL_LOGE("Recipe object did not parse into a map.");
            return GGL_ERR_INVALID;
        }

        ret = ggl_map_validate(
            recipe_obj.map,
            GGL_MAP_SCHEMA(
                { GGL_STR("ComponentDependencies"),
                  false,
                  GGL_TYPE_MAP,
                  &component_dependencies },
            )
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        if (component_dependencies) {
            GGL_MAP_FOREACH(dependency, component_dependencies->map) {
                if (dependency->val.type != GGL_TYPE_MAP) {
                    GGL_LOGE(
                        "Component dependency in recipe does not have map data"
                    );
                    return GGL_ERR_INVALID;
                }
                GglObject *dep_version_requirement = NULL;
                ret = ggl_map_validate(
                    dependency->val.map,
                    GGL_MAP_SCHEMA(
                        { GGL_STR("VersionRequirement"),
                          true,
                          GGL_TYPE_BUF,
                          &dep_version_requirement },
                    )
                );
                if (ret != GGL_ERR_OK) {
                    return ret;
                }

                // If we already resolved the component version, check that it
                // still satisfies the new requirement and fail otherwise.
                GglObject *already_resolved_version;
                ret = ggl_map_validate(
                    resolved_components_kv_vec->map,
                    GGL_MAP_SCHEMA(
                        { dependency->key,
                          false,
                          GGL_TYPE_BUF,
                          &already_resolved_version },
                    )
                );
                if (ret != GGL_ERR_OK) {
                    return ret;
                }
                if (already_resolved_version) {
                    bool meets_requirements = is_in_range(
                        already_resolved_version->buf,
                        dep_version_requirement->buf
                    );
                    if (!meets_requirements) {
                        GGL_LOGE("Already resolved component does not meet new "
                                 "dependency requirement, failing dependency "
                                 "resolution.");
                        return GGL_ERR_FAILURE;
                    }
                }

                if (!already_resolved_version) {
                    // If we haven't resolved it yet, check if we have an
                    // existing requirement and append the new requirement if
                    // so.
                    GglObject *existing_requirements;
                    ret = ggl_map_validate(
                        components_to_resolve.map,
                        GGL_MAP_SCHEMA(
                            { dependency->key,
                              false,
                              GGL_TYPE_BUF,
                              &existing_requirements },
                        )
                    );
                    if (ret != GGL_ERR_OK) {
                        return ret;
                    }
                    if (existing_requirements) {
                        static uint8_t new_req_buf[PATH_MAX];
                        GglByteVec new_req_vec = GGL_BYTE_VEC(new_req_buf);
                        ret = ggl_byte_vec_append(
                            &new_req_vec, existing_requirements->buf
                        );
                        ggl_byte_vec_chain_push(&ret, &new_req_vec, ' ');
                        ggl_byte_vec_chain_append(
                            &ret, &new_req_vec, dep_version_requirement->buf
                        );
                        if (ret != GGL_ERR_OK) {
                            GGL_LOGE("Failed to create new requirements for "
                                     "dependency version.");
                            return ret;
                        }

                        *existing_requirements = GGL_OBJ(new_req_vec.buf);
                    }

                    // If we haven't resolved it yet, and it doesn't have an
                    // existing requirement, add it.
                    if (!existing_requirements) {
                        GglBuffer name_key_buf;
                        ret = ggl_buf_clone(
                            dependency->key, alloc, &name_key_buf
                        );
                        if (ret != GGL_ERR_OK) {
                            return ret;
                        }

                        GglBuffer vers_key_buf;
                        ret = ggl_buf_clone(
                            dep_version_requirement->buf,
                            &version_requirements_balloc.alloc,
                            &vers_key_buf
                        );
                        if (ret != GGL_ERR_OK) {
                            return ret;
                        }

                        ret = ggl_kv_vec_push(
                            &components_to_resolve,
                            (GglKV) { name_key_buf, GGL_OBJ(vers_key_buf) }
                        );
                        if (ret != GGL_ERR_OK) {
                            return ret;
                        }
                    }
                }
            }
        }
    }
    return GGL_ERR_OK;
}

static GglError open_component_artifacts_dir(
    int artifact_store_fd,
    GglBuffer component_name,
    GglBuffer component_version,
    int *version_fd
) {
    int component_fd = -1;
    GglError ret = ggl_dir_openat(
        artifact_store_fd, component_name, O_PATH, true, &component_fd
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_DEFER(ggl_close, component_fd);
    return ggl_dir_openat(
        component_fd, component_version, O_PATH, true, version_fd
    );
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
            GGL_LOGE("Failed to copy recipes.");
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
            GGL_LOGE("Failed to copy artifacts.");
            return;
        }
    }

    // TODO: Add dependency resolution process that will also check local store.
    if (deployment->cloud_root_components_to_add.len != 0) {
        GglKVVec resolved_components_kv_vec = GGL_KV_VEC((GglKV[64]) { 0 });
        static uint8_t resolve_dependencies_mem[8192] = { 0 };
        GglBumpAlloc resolve_dependencies_balloc
            = ggl_bump_alloc_init(GGL_BUF(resolve_dependencies_mem));
        GglError ret = resolve_dependencies(
            deployment->cloud_root_components_to_add,
            args,
            &resolve_dependencies_balloc.alloc,
            &resolved_components_kv_vec
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to do dependency resolution for deployment, failing "
                "deployment."
            );
            return;
        }

        GglByteVec region = GGL_BYTE_VEC(config.region);
        ret = get_region(&region);
        if (ret != GGL_ERR_OK) {
            return;
        }
        CertificateDetails iot_credentials
            = { .gghttplib_cert_path = config.cert_path,
                .gghttplib_p_key_path = config.pkey_path,
                .gghttplib_root_ca_path = config.rootca_path };
        TesCredentials tes_credentials = { .aws_region = region.buf };
        ret = get_tes_credentials(&tes_credentials);
        if (ret != GGL_ERR_OK) {
            return;
        }

        int artifact_store_fd = -1;
        ret = ggl_dir_openat(
            root_path_fd,
            GGL_STR("packages/artifacts"),
            O_PATH,
            true,
            &artifact_store_fd
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to open artifact store");
            return;
        }

        int artifact_archive_fd = -1;
        ret = ggl_dir_openat(
            root_path_fd,
            GGL_STR("packages/artifacts-unarchived"),
            O_PATH,
            true,
            &artifact_archive_fd
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to open archive store.");
            return;
        }

        GGL_MAP_FOREACH(pair, resolved_components_kv_vec.map) {
            int component_artifacts_fd = -1;
            open_component_artifacts_dir(
                artifact_store_fd,
                pair->key,
                pair->val.buf,
                &component_artifacts_fd
            );
            int component_archive_dir_fd = -1;
            open_component_artifacts_dir(
                artifact_archive_fd,
                pair->key,
                pair->val.buf,
                &component_archive_dir_fd
            );
            GglObject recipe_obj;
            static uint8_t recipe_mem[8192] = { 0 };
            static uint8_t component_arn_buffer[256];
            GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(recipe_mem));
            ret = ggl_recipe_get_from_file(
                args->root_path_fd,
                pair->key,
                pair->val.buf,
                &balloc.alloc,
                &recipe_obj
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to validate and decode recipe");
                return;
            }

            GglBuffer component_arn = GGL_BUF(component_arn_buffer);
            if (component_arn.data == NULL) {
                GGL_LOGE("Failed to retrieve arn");
                return;
            }

            ret = ggl_gg_config_read_str(
                GGL_BUF_LIST(GGL_STR("services"), pair->key, GGL_STR("arn")),
                &component_arn
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to retrieve arn");
                return;
            }

            ret = get_recipe_artifacts(
                component_arn,
                tes_credentials,
                iot_credentials,
                recipe_obj.map,
                component_artifacts_fd,
                component_archive_dir_fd
            );

            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to get artifacts from recipe.");
                return;
            }

            // FIXME: Don't only support yaml extensions.
            static uint8_t recipe_path_buf[PATH_MAX];
            GglByteVec recipe_path_vec = GGL_BYTE_VEC(recipe_path_buf);
            ret = ggl_byte_vec_append(&recipe_path_vec, args->root_path);
            ggl_byte_vec_chain_append(
                &ret, &recipe_path_vec, GGL_STR("/packages/recipes/")
            );
            ggl_byte_vec_chain_append(&ret, &recipe_path_vec, pair->key);
            ggl_byte_vec_chain_push(&ret, &recipe_path_vec, '-');
            ggl_byte_vec_chain_append(&ret, &recipe_path_vec, pair->val.buf);
            ggl_byte_vec_chain_append(&ret, &recipe_path_vec, GGL_STR(".yaml"));
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to create recipe path.");
                return;
            }

            static uint8_t recipe_runner_path_buf[PATH_MAX];
            GglByteVec recipe_runner_path_vec
                = GGL_BYTE_VEC(recipe_runner_path_buf);
            ret = ggl_byte_vec_append(
                &recipe_runner_path_vec,
                ggl_buffer_from_null_term((char *) args->bin_path)
            );
            ggl_byte_vec_chain_append(
                &ret, &recipe_runner_path_vec, GGL_STR("recipe-runner")
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to create recipe runner path.");
                return;
            }

            char *thing_name = NULL;
            ret = get_thing_name(&thing_name);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to get thing name.");
                return;
            }

            char *root_ca_path = NULL;
            ret = get_root_ca_path(&root_ca_path);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to get rootCaPath.");
                return;
            }

            char *tes_cred_url = NULL;
            ret = get_tes_cred_url(&tes_cred_url);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to get tes credentials url.");
                return;
            }

            char *posix_user = NULL;
            ret = get_posix_user(&posix_user);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to get posix_user.");
                return;
            }
            if (strlen(posix_user) < 1) {
                GGL_LOGE("Run with default posix user is not set.");
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

            Recipe2UnitArgs recipe2unit_args
                = { .user = posix_user, .group = group };
            memcpy(
                recipe2unit_args.recipe_path,
                recipe_path_vec.buf.data,
                recipe_path_vec.buf.len
            );
            memcpy(
                recipe2unit_args.recipe_runner_path,
                recipe_runner_path_vec.buf.data,
                recipe_runner_path_vec.buf.len
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
                    GGL_LOGE("ComponentConfiguration is not a map type");
                    return;
                }

                if (ggl_map_get(
                        intermediate_obj->map,
                        GGL_STR("defaultconfiguration"),
                        &default_config_obj
                    )) {
                    ret = ggl_gg_config_write(
                        GGL_BUF_LIST(
                            GGL_STR("services"),
                            component_name->buf,
                            GGL_STR("configuration")
                        ),
                        *default_config_obj,
                        0
                    );

                    if (ret != GGL_ERR_OK) {
                        GGL_LOGE("Failed to send default config to ggconfigd.");
                        return;
                    }

                    // add component version to the config
                    ret = ggl_gg_config_write(
                        GGL_BUF_LIST(
                            GGL_STR("services"),
                            component_name->buf,
                            GGL_STR("version")
                        ),
                        pair->val,
                        0
                    );
                    if (ret != GGL_ERR_OK) {
                        GGL_LOGE(
                            "Failed to send component version to ggconfigd."
                        );
                        return;
                    }
                } else {
                    GGL_LOGI("DefaultConfiguration not found in the recipe.");
                }
            } else {
                GGL_LOGI("ComponentConfiguration not found in the recipe");
            }

            // TODO: add install file processing logic here.

            static uint8_t service_file_path_buf[PATH_MAX];
            GglByteVec service_file_path_vec
                = GGL_BYTE_VEC(service_file_path_buf);
            ret = ggl_byte_vec_append(&service_file_path_vec, GGL_STR("ggl."));
            ggl_byte_vec_chain_append(&ret, &service_file_path_vec, pair->key);
            ggl_byte_vec_chain_append(
                &ret, &service_file_path_vec, GGL_STR(".service")
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to create service file path.");
                return;
            }

            static uint8_t link_command_buf[PATH_MAX];
            GglByteVec link_command_vec = GGL_BYTE_VEC(link_command_buf);
            ret = ggl_byte_vec_append(
                &link_command_vec, GGL_STR("sudo systemctl link ")
            );
            ggl_byte_vec_chain_append(&ret, &link_command_vec, args->root_path);
            ggl_byte_vec_chain_push(&ret, &link_command_vec, '/');
            ggl_byte_vec_chain_append(
                &ret, &link_command_vec, service_file_path_vec.buf
            );
            ggl_byte_vec_chain_push(&ret, &link_command_vec, '\0');
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to create systemctl link command.");
                return;
            }

            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            int system_ret = system((char *) link_command_vec.buf.data);
            if (WIFEXITED(system_ret)) {
                if (WEXITSTATUS(system_ret) != 0) {
                    GGL_LOGE("systemctl link failed");
                    return;
                }
                GGL_LOGI(
                    "systemctl link exited with child status %d\n",
                    WEXITSTATUS(system_ret)
                );
            } else {
                GGL_LOGE("systemctl link did not exit normally");
                return;
            }

            static uint8_t start_command_buf[PATH_MAX];
            GglByteVec start_command_vec = GGL_BYTE_VEC(start_command_buf);
            ret = ggl_byte_vec_append(
                &start_command_vec, GGL_STR("sudo systemctl start ")
            );
            ggl_byte_vec_chain_append(
                &ret, &start_command_vec, service_file_path_vec.buf
            );
            ggl_byte_vec_chain_push(&ret, &start_command_vec, '\0');
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to create systemctl start command.");
                return;
            }

            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            system_ret = system((char *) start_command_vec.buf.data);
            if (WIFEXITED(system_ret)) {
                if (WEXITSTATUS(system_ret) != 0) {
                    GGL_LOGE("systemctl start failed");
                    return;
                }
                GGL_LOGI(
                    "systemctl start exited with child status %d\n",
                    WEXITSTATUS(system_ret)
                );
            } else {
                GGL_LOGE("systemctl start did not exit normally");
                return;
            }

            static uint8_t enable_command_buf[PATH_MAX];
            GglByteVec enable_command_vec = GGL_BYTE_VEC(enable_command_buf);
            ret = ggl_byte_vec_append(
                &enable_command_vec, GGL_STR("sudo systemctl enable ")
            );
            ggl_byte_vec_chain_append(
                &ret, &enable_command_vec, service_file_path_vec.buf
            );
            ggl_byte_vec_chain_push(&ret, &enable_command_vec, '\0');
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to create systemctl enable command.");
                return;
            }

            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            system_ret = system((char *) enable_command_vec.buf.data);
            if (WIFEXITED(system_ret)) {
                if (WEXITSTATUS(system_ret) != 0) {
                    GGL_LOGE("systemctl enable failed");
                    return;
                }
                GGL_LOGI(
                    "systemctl enable exited with child status %d\n",
                    WEXITSTATUS(system_ret)
                );
            } else {
                GGL_LOGE("systemctl enable did not exit normally");
                return;
            }
        }
    }

    if (deployment->root_component_versions_to_add.len != 0) {
        GGL_MAP_FOREACH(pair, deployment->root_component_versions_to_add) {
            if (pair->val.type != GGL_TYPE_BUF) {
                GGL_LOGE("Component version is not a buffer.");
                return;
            }

            // FIXME: Don't only support yaml extensions.
            static uint8_t recipe_path_buf[PATH_MAX];
            GglByteVec recipe_path_vec = GGL_BYTE_VEC(recipe_path_buf);
            GglError ret
                = ggl_byte_vec_append(&recipe_path_vec, args->root_path);
            ggl_byte_vec_chain_append(
                &ret, &recipe_path_vec, GGL_STR("/packages/recipes/")
            );
            ggl_byte_vec_chain_append(&ret, &recipe_path_vec, pair->key);
            ggl_byte_vec_chain_push(&ret, &recipe_path_vec, '-');
            ggl_byte_vec_chain_append(&ret, &recipe_path_vec, pair->val.buf);
            ggl_byte_vec_chain_append(&ret, &recipe_path_vec, GGL_STR(".yaml"));
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to create recipe path.");
                return;
            }

            static uint8_t recipe_runner_path_buf[PATH_MAX];
            GglByteVec recipe_runner_path_vec
                = GGL_BYTE_VEC(recipe_runner_path_buf);
            ret = ggl_byte_vec_append(
                &recipe_runner_path_vec,
                ggl_buffer_from_null_term((char *) args->bin_path)
            );
            ggl_byte_vec_chain_append(
                &ret, &recipe_runner_path_vec, GGL_STR("recipe-runner")
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to create recipe runner path.");
                return;
            }

            char *thing_name = NULL;
            ret = get_thing_name(&thing_name);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to get thing name.");
                return;
            }

            GglByteVec region = GGL_BYTE_VEC(config.region);
            ret = get_region(&region);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to get region.");
                return;
            }

            char *root_ca_path = NULL;
            ret = get_root_ca_path(&root_ca_path);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to get rootCaPath.");
                return;
            }

            char *posix_user = NULL;
            ret = get_posix_user(&posix_user);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to get posix_user.");
                return;
            }
            if (strlen(posix_user) < 1) {
                GGL_LOGE("Run with default posix user is not set.");
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

            Recipe2UnitArgs recipe2unit_args
                = { .group = group, .user = posix_user };
            GGL_LOGI("Recipe path %s", recipe_path_vec.buf.data);
            memcpy(
                recipe2unit_args.recipe_path,
                recipe_path_vec.buf.data,
                recipe_path_vec.buf.len
            );
            memcpy(
                recipe2unit_args.recipe_runner_path,
                recipe_runner_path_vec.buf.data,
                recipe_runner_path_vec.buf.len
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
                    GGL_LOGE("ComponentConfiguration is not a map type");
                    return;
                }

                if (ggl_map_get(
                        intermediate_obj->map,
                        GGL_STR("defaultconfiguration"),
                        &default_config_obj
                    )) {
                    ret = ggl_gg_config_write(
                        GGL_BUF_LIST(
                            GGL_STR("services"),
                            component_name->buf,
                            GGL_STR("configuration")
                        ),
                        *default_config_obj,
                        0
                    );

                    if (ret != GGL_ERR_OK) {
                        GGL_LOGE("Failed to send default config to ggconfigd.");
                        return;
                    }
                } else {
                    GGL_LOGI("DefaultConfiguration not found in the recipe.");
                }
            } else {
                GGL_LOGI("ComponentConfiguration not found in the recipe");
            }

            // TODO: add install file processing logic here.

            static uint8_t service_file_path_buf[PATH_MAX];
            GglByteVec service_file_path_vec
                = GGL_BYTE_VEC(service_file_path_buf);
            ret = ggl_byte_vec_append(&service_file_path_vec, GGL_STR("ggl."));
            ggl_byte_vec_chain_append(&ret, &service_file_path_vec, pair->key);
            ggl_byte_vec_chain_append(
                &ret, &service_file_path_vec, GGL_STR(".service")
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to create service file path.");
                return;
            }

            static uint8_t link_command_buf[PATH_MAX];
            GglByteVec link_command_vec = GGL_BYTE_VEC(link_command_buf);
            ret = ggl_byte_vec_append(
                &link_command_vec, GGL_STR("sudo systemctl link ")
            );
            ggl_byte_vec_chain_append(&ret, &link_command_vec, args->root_path);
            ggl_byte_vec_chain_push(&ret, &link_command_vec, '/');
            ggl_byte_vec_chain_append(
                &ret, &link_command_vec, service_file_path_vec.buf
            );
            ggl_byte_vec_chain_push(&ret, &link_command_vec, '\0');
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to create systemctl link command.");
                return;
            }
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            int system_ret = system((char *) link_command_vec.buf.data);
            if (WIFEXITED(system_ret)) {
                if (WEXITSTATUS(system_ret) != 0) {
                    GGL_LOGE("systemctl link failed");
                    return;
                }
                GGL_LOGI(
                    "systemctl link exited with child status %d\n",
                    WEXITSTATUS(system_ret)
                );
            } else {
                GGL_LOGE("systemctl link did not exit normally");
                return;
            }

            static uint8_t start_command_buf[PATH_MAX];
            GglByteVec start_command_vec = GGL_BYTE_VEC(start_command_buf);
            ret = ggl_byte_vec_append(
                &start_command_vec, GGL_STR("sudo systemctl start ")
            );
            ggl_byte_vec_chain_append(
                &ret, &start_command_vec, service_file_path_vec.buf
            );
            ggl_byte_vec_chain_push(&ret, &start_command_vec, '\0');
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to create systemctl start command.");
                return;
            }
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            system_ret = system((char *) start_command_vec.buf.data);
            if (WIFEXITED(system_ret)) {
                if (WEXITSTATUS(system_ret) != 0) {
                    GGL_LOGE("systemctl start failed");
                    return;
                }
                GGL_LOGI(
                    "systemctl start exited with child status %d\n",
                    WEXITSTATUS(system_ret)
                );
            } else {
                GGL_LOGE("systemctl start did not exit normally");
                return;
            }

            static uint8_t enable_command_buf[PATH_MAX];
            GglByteVec enable_command_vec = GGL_BYTE_VEC(enable_command_buf);
            ret = ggl_byte_vec_append(
                &enable_command_vec, GGL_STR("sudo systemctl enable ")
            );
            ggl_byte_vec_chain_append(
                &ret, &enable_command_vec, service_file_path_vec.buf
            );
            ggl_byte_vec_chain_push(&ret, &enable_command_vec, '\0');
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to create systemctl enable command.");
                return;
            }
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            system_ret = system((char *) enable_command_vec.buf.data);
            if (WIFEXITED(system_ret)) {
                if (WEXITSTATUS(system_ret) != 0) {
                    GGL_LOGE("systemctl enable failed");
                    return;
                }
                GGL_LOGI(
                    "systemctl enable exited with child status %d\n",
                    WEXITSTATUS(system_ret)
                );
            } else {
                GGL_LOGE("systemctl enable did not exit normally");
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

        GGL_LOGI("Processing incoming deployment.");

        update_current_jobs_deployment(
            deployment->deployment_id, GGL_STR("IN_PROGRESS")
        );

        handle_deployment(deployment, args);

        GGL_LOGD("Completed deployment.");

        // TODO: need error details from handle_deployment
        update_current_jobs_deployment(
            deployment->deployment_id, GGL_STR("SUCCEEDED")
        );

        ggl_deployment_release(deployment);
    }
}

void *ggl_deployment_handler_thread(void *ctx) {
    GGL_LOGD("Starting deployment processing thread.");

    (void) ggl_deployment_listen(ctx);

    GGL_LOGE("Deployment thread exiting due to failure.");

    // This is safe as long as only this thread will ever call exit.

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);

    return NULL;
}
