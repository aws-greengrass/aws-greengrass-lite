// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "deployment_handler.h"
#include "bootstrap_manager.h"
#include "component_config.h"
#include "component_manager.h"
#include "deployment_model.h"
#include "deployment_queue.h"
#include "iot_jobs_listener.h"
#include "priv_io.h"
#include "stale_component.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ggl/arena.h>
#include <ggl/backoff.h>
#include <ggl/base64.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/core_bus/gg_healthd.h>
#include <ggl/core_bus/sub_response.h>
#include <ggl/digest.h>
#include <ggl/docker_client.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/flags.h>
#include <ggl/http.h>
#include <ggl/json_decode.h>
#include <ggl/json_encode.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/nucleus/constants.h>
#include <ggl/object.h>
#include <ggl/process.h>
#include <ggl/recipe.h>
#include <ggl/recipe2unit.h>
#include <ggl/semver.h>
#include <ggl/uri.h>
#include <ggl/utils.h>
#include <ggl/vector.h>
#include <ggl/zip.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_DECODE_BUF_LEN 4096
#define DEPLOYMENT_TARGET_NAME_MAX_CHARS 128
#define MAX_DEPLOYMENT_TARGETS 100

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

// vector to track successfully deployed components to be saved for bootstrap
// component name -> map of lifecycle state and version
// static GglKVVec deployed_components = GGL_KV_VEC((GglKV[64]) { 0 });

static SigV4Details sigv4_from_tes(
    TesCredentials credentials, GglBuffer aws_service
) {
    return (SigV4Details) { .aws_region = credentials.aws_region,
                            .aws_service = aws_service,
                            .access_key_id = credentials.access_key_id,
                            .secret_access_key = credentials.secret_access_key,
                            .session_token = credentials.session_token };
}

static GglError merge_dir_to(GglBuffer source, const char *dir) {
    const char *mkdir[] = { "mkdir", "-p", dir, NULL };
    GglError ret = ggl_process_call(mkdir);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // Append /. so that contents get copied, not dir
    static char source_path[PATH_MAX];
    GglByteVec source_path_vec = GGL_BYTE_VEC(source_path);
    ret = ggl_byte_vec_append(&source_path_vec, source);
    ggl_byte_vec_chain_append(&ret, &source_path_vec, GGL_STR("/.\0"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    const char *cp[] = { "cp", "-RP", source_path, dir, NULL };
    return ggl_process_call(cp);
}

static GglError get_thing_name(char **thing_name) {
    static uint8_t resp_mem[129] = { 0 };
    GglArena alloc = ggl_arena_init(
        ggl_buffer_substr(GGL_BUF(resp_mem), 0, sizeof(resp_mem) - 1)
    );
    GglBuffer resp = { 0 };

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("thingName")), &alloc, &resp
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
    GglArena alloc = ggl_arena_init(GGL_BUF(resp_mem));
    GglBuffer resp;

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("configuration"),
            GGL_STR("awsRegion")
        ),
        &alloc,
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
    GglArena alloc = ggl_arena_init(
        ggl_buffer_substr(GGL_BUF(resp_mem), 0, sizeof(resp_mem) - 1)
    );
    GglBuffer resp;

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootCaPath")), &alloc, &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get rootCaPath from config.");
        return ret;
    }
    resp.data[resp.len] = '\0';

    *root_ca_path = (char *) resp.data;
    return GGL_ERR_OK;
}

static GglError get_posix_user(char **posix_user) {
    static uint8_t resp_mem[129] = { 0 };
    GglArena alloc = ggl_arena_init(
        ggl_buffer_substr(GGL_BUF(resp_mem), 0, sizeof(resp_mem) - 1)
    );
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("configuration"),
            GGL_STR("runWithDefault"),
            GGL_STR("posixUser")
        ),
        &alloc,
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
    GglMap params = GGL_MAP(ggl_kv(
        GGL_STR("key_path"),
        ggl_obj_list(GGL_LIST(
            ggl_obj_buf(GGL_STR("services")),
            ggl_obj_buf(GGL_STR("aws.greengrass.NucleusLite")),
            ggl_obj_buf(GGL_STR("configuration")),
            ggl_obj_buf(GGL_STR("iotDataEndpoint"))
        ))
    ));

    static uint8_t resp_mem[128] = { 0 };
    GglArena alloc = ggl_arena_init(
        ggl_buffer_substr(GGL_BUF(resp_mem), 0, sizeof(resp_mem) - 1)
    );

    GglObject resp;
    GglError ret = ggl_call(
        GGL_STR("gg_config"), GGL_STR("read"), params, NULL, &alloc, &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get dataplane endpoint from config.");
        return ret;
    }
    if (ggl_obj_type(resp) != GGL_TYPE_BUF) {
        GGL_LOGE("Configuration dataplane endpoint is not a string.");
        return GGL_ERR_INVALID;
    }

    return ggl_byte_vec_append(endpoint, ggl_obj_into_buf(resp));
}

static GglError get_data_port(GglByteVec *port) {
    GglMap params = GGL_MAP(ggl_kv(
        GGL_STR("key_path"),
        ggl_obj_list(GGL_LIST(
            ggl_obj_buf(GGL_STR("services")),
            ggl_obj_buf(GGL_STR("aws.greengrass.NucleusLite")),
            ggl_obj_buf(GGL_STR("configuration")),
            ggl_obj_buf(GGL_STR("greengrassDataPlanePort"))
        ))
    ));

    static uint8_t resp_mem[128] = { 0 };
    GglArena alloc = ggl_arena_init(
        ggl_buffer_substr(GGL_BUF(resp_mem), 0, sizeof(resp_mem) - 1)
    );

    GglObject resp;
    GglError ret = ggl_call(
        GGL_STR("gg_config"), GGL_STR("read"), params, NULL, &alloc, &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get dataplane port from config.");
        return ret;
    }
    if (ggl_obj_type(resp) != GGL_TYPE_BUF) {
        GGL_LOGE("Configuration dataplane port is not a string.");
        return GGL_ERR_INVALID;
    }

    return ggl_byte_vec_append(port, ggl_obj_into_buf(resp));
}

static GglError get_private_key_path(GglByteVec *pkey_path) {
    GglMap params = GGL_MAP(ggl_kv(
        GGL_STR("key_path"),
        ggl_obj_list(GGL_LIST(
            ggl_obj_buf(GGL_STR("system")),
            ggl_obj_buf(GGL_STR("privateKeyPath"))
        ))
    ));

    uint8_t resp_mem[128] = { 0 };
    GglArena alloc = ggl_arena_init(
        ggl_buffer_substr(GGL_BUF(resp_mem), 0, sizeof(resp_mem) - 1)
    );

    GglObject resp;
    GglError ret = ggl_call(
        GGL_STR("gg_config"), GGL_STR("read"), params, NULL, &alloc, &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get private key path from config.");
        return ret;
    }
    if (ggl_obj_type(resp) != GGL_TYPE_BUF) {
        GGL_LOGE("Configuration private key path is not a string.");
        return GGL_ERR_INVALID;
    }

    ggl_byte_vec_chain_append(&ret, pkey_path, ggl_obj_into_buf(resp));
    ggl_byte_vec_chain_push(&ret, pkey_path, '\0');
    return ret;
}

static GglError get_cert_path(GglByteVec *cert_path) {
    GglMap params = GGL_MAP(ggl_kv(
        GGL_STR("key_path"),
        ggl_obj_list(GGL_LIST(
            ggl_obj_buf(GGL_STR("system")),
            ggl_obj_buf(GGL_STR("certificateFilePath"))
        ))
    ));

    static uint8_t resp_mem[128] = { 0 };
    GglArena alloc = ggl_arena_init(
        ggl_buffer_substr(GGL_BUF(resp_mem), 0, sizeof(resp_mem) - 1)
    );

    GglObject resp;
    GglError ret = ggl_call(
        GGL_STR("gg_config"), GGL_STR("read"), params, NULL, &alloc, &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get certificate path from config.");
        return ret;
    }
    if (ggl_obj_type(resp) != GGL_TYPE_BUF) {
        GGL_LOGE("Configuration certificate path is not a string.");
        return GGL_ERR_INVALID;
    }

    ggl_byte_vec_chain_append(&ret, cert_path, ggl_obj_into_buf(resp));
    ggl_byte_vec_chain_push(&ret, cert_path, '\0');
    return ret;
}

static GglError get_rootca_path(GglByteVec *rootca_path) {
    GglMap params = GGL_MAP(ggl_kv(
        GGL_STR("key_path"),
        ggl_obj_list(GGL_LIST(
            ggl_obj_buf(GGL_STR("system")), ggl_obj_buf(GGL_STR("rootCaPath"))
        ))
    ));

    static uint8_t resp_mem[128] = { 0 };
    GglArena alloc = ggl_arena_init(
        ggl_buffer_substr(GGL_BUF(resp_mem), 0, sizeof(resp_mem) - 1)
    );

    GglObject resp;
    GglError ret = ggl_call(
        GGL_STR("gg_config"), GGL_STR("read"), params, NULL, &alloc, &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get rootca path from config.");
        return ret;
    }
    if (ggl_obj_type(resp) != GGL_TYPE_BUF) {
        GGL_LOGE("Configuration rootca path is not a string.");
        return GGL_ERR_INVALID;
    }

    ggl_byte_vec_chain_append(&ret, rootca_path, ggl_obj_into_buf(resp));
    ggl_byte_vec_chain_push(&ret, rootca_path, '\0');
    return ret;
}

static GglError get_tes_credentials(TesCredentials *tes_creds) {
    GglObject *aws_access_key_id = NULL;
    GglObject *aws_secret_access_key = NULL;
    GglObject *aws_session_token = NULL;

    static uint8_t credentials_alloc[1500];
    static GglBuffer tesd = GGL_STR("aws_iot_tes");
    GglObject result;
    GglMap params = { 0 };
    GglArena credential_alloc = ggl_arena_init(GGL_BUF(credentials_alloc));

    GglError ret = ggl_call(
        tesd,
        GGL_STR("request_credentials"),
        params,
        NULL,
        &credential_alloc,
        &result
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get TES credentials.");
        return GGL_ERR_FAILURE;
    }

    ret = ggl_map_validate(
        ggl_obj_into_map(result),
        GGL_MAP_SCHEMA(
            { GGL_STR("accessKeyId"),
              GGL_REQUIRED,
              GGL_TYPE_BUF,
              &aws_access_key_id },
            { GGL_STR("secretAccessKey"),
              GGL_REQUIRED,
              GGL_TYPE_BUF,
              &aws_secret_access_key },
            { GGL_STR("sessionToken"),
              GGL_REQUIRED,
              GGL_TYPE_BUF,
              &aws_session_token },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to validate TES credentials."

        );
        return GGL_ERR_FAILURE;
    }
    tes_creds->access_key_id = ggl_obj_into_buf(*aws_access_key_id);
    tes_creds->secret_access_key = ggl_obj_into_buf(*aws_secret_access_key);
    tes_creds->session_token = ggl_obj_into_buf(*aws_session_token);
    return GGL_ERR_OK;
}

typedef struct {
    const char *url_for_sigv4_download;
    GglBuffer host;
    GglBuffer file_path;
    SigV4Details sigv4_details;

    // reset response_data for next attempt
    GglError (*retry_cleanup_fn)(void *);
    void *response_data;

    // Needed to propagate errors when retrying is impossible.
    GglError err;
} DownloadRequestRetryCtx;

static GglError retry_download_wrapper(void *ctx) {
    DownloadRequestRetryCtx *retry_ctx = (DownloadRequestRetryCtx *) ctx;
    uint16_t http_response_code;

    GglError ret = sigv4_download(
        retry_ctx->url_for_sigv4_download,
        retry_ctx->host,
        retry_ctx->file_path,
        *(int *) retry_ctx->response_data,
        retry_ctx->sigv4_details,
        &http_response_code
    );
    if (http_response_code == (uint16_t) 403) {
        GglError err = retry_ctx->retry_cleanup_fn(retry_ctx->response_data);
        GGL_LOGE(
            "Artifact download attempt failed with 403. Retrying with backoff."
        );
        if (err != GGL_ERR_OK) {
            retry_ctx->err = err;
            return GGL_ERR_OK;
        }
        return GGL_ERR_FAILURE;
    }
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Artifact download attempt failed due to error: %d", ret

        );
        retry_ctx->err = ret;
        return GGL_ERR_OK;
    }

    retry_ctx->err = ret;
    return GGL_ERR_OK;
}

// TODO: Refactor to delete the file and get the new fd instead of truncating
// the file
static GglError truncate_s3_file_on_failure(void *response_data) {
    int fd = *(int *) response_data;

    int ret;
    do {
        ret = ftruncate(fd, 0);
    } while ((ret == -1) && (errno == EINTR));

    if (ret == -1) {
        GGL_LOGE("Failed to truncate fd for write (errno=%d).", errno);
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}

static GglError retryable_download_request(
    const char *url_for_sigv4_download,
    GglBuffer host,
    GglBuffer file_path,
    int artifact_fd,
    SigV4Details sigv4_details
) {
    DownloadRequestRetryCtx ctx
        = { .url_for_sigv4_download = url_for_sigv4_download,
            .host = host,
            .file_path = file_path,
            .sigv4_details = sigv4_details,
            .response_data = (void *) &artifact_fd,
            .retry_cleanup_fn = truncate_s3_file_on_failure,
            .err = GGL_ERR_OK };

    GglError ret
        = ggl_backoff(3000, 64000, 3, retry_download_wrapper, (void *) &ctx);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Artifact download attempt failed; retries exhausted.");
        return ret;
    }
    if (ctx.err != GGL_ERR_OK) {
        return ctx.err;
    }
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
    size_t start_loc = 0;
    size_t end_loc = 0;
    size_t file_name_end = 0;
    ggl_byte_vec_chain_append(&error, &url_vec, GGL_STR("https://"));
    start_loc = url_vec.buf.len;
    ggl_byte_vec_chain_append(&error, &url_vec, uri_info.host);
    ggl_byte_vec_chain_append(&error, &url_vec, GGL_STR(".s3."));
    ggl_byte_vec_chain_append(&error, &url_vec, credentials.aws_region);
    ggl_byte_vec_chain_append(&error, &url_vec, GGL_STR(".amazonaws.com/"));
    end_loc = url_vec.buf.len - 1;
    ggl_byte_vec_chain_append(&error, &url_vec, uri_info.path);
    file_name_end = url_vec.buf.len;
    ggl_byte_vec_chain_push(&error, &url_vec, '\0');
    if (error != GGL_ERR_OK) {
        return error;
    }

    return retryable_download_request(
        (const char *) url_vec.buf.data,
        (GglBuffer) { .data = &scratch_buffer.data[start_loc],
                      .len = end_loc - start_loc },
        (GglBuffer) { .data = &scratch_buffer.data[end_loc],
                      .len = file_name_end - end_loc },
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
    GglArena json_bump = ggl_arena_init(scratch_buffer);
    GglObject response_obj;
    err = ggl_json_decode_destructive(
        response_buffer, &json_bump, &response_obj
    );
    if (err != GGL_ERR_OK) {
        return err;
    }
    if (ggl_obj_type(response_obj) != GGL_TYPE_MAP) {
        return GGL_ERR_PARSE;
    }
    GglObject *presigned_url_obj;
    err = ggl_map_validate(
        ggl_obj_into_map(response_obj),
        GGL_MAP_SCHEMA({ GGL_STR("preSignedUrl"),
                         GGL_REQUIRED,
                         GGL_TYPE_BUF,
                         &presigned_url_obj })
    );
    if (err != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }
    GglBuffer presigned_url = ggl_obj_into_buf(*presigned_url_obj);

    // Should be OK to null-terminate this buffer;
    // it's in the middle of a JSON blob.
    presigned_url.data[presigned_url.len] = '\0';

    GGL_LOGI("Getting presigned S3 URL artifact");

    return generic_download((const char *) (presigned_url.data), artifact_fd);
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
        GGL_LOGE("Failed to open unarchived artifact location.");
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
    int component_archive_store_fd,
    GglDigest digest_context
) {
    GglList artifacts = { 0 };
    GglError error = ggl_get_recipe_artifacts_for_platform(recipe, &artifacts);
    if (error != GGL_ERR_OK) {
        return error;
    }

    bool ecr_logged_in = false;
    for (size_t i = 0; i < artifacts.len; ++i) {
        uint8_t decode_buffer[MAX_DECODE_BUF_LEN];
        if (ggl_obj_type(artifacts.items[i]) != GGL_TYPE_MAP) {
            return GGL_ERR_PARSE;
        }
        GglObject *uri_obj = NULL;
        GglObject *unarchive_obj = NULL;
        GglObject *expected_digest_obj = NULL;
        GglObject *algorithm = NULL;

        GglError err = ggl_map_validate(
            ggl_obj_into_map(artifacts.items[i]),
            GGL_MAP_SCHEMA(
                { GGL_STR("Uri"), GGL_REQUIRED, GGL_TYPE_BUF, &uri_obj },
                { GGL_STR("Unarchive"),
                  GGL_OPTIONAL,
                  GGL_TYPE_BUF,
                  &unarchive_obj },
                { GGL_STR("Digest"),
                  GGL_OPTIONAL,
                  GGL_TYPE_BUF,
                  &expected_digest_obj },
                { GGL_STR("Algorithm"), GGL_OPTIONAL, GGL_TYPE_BUF, &algorithm }
            )
        );

        if (err != GGL_ERR_OK) {
            GGL_LOGE("Failed to validate recipe artifact");
            return GGL_ERR_PARSE;
        }

        GglUriInfo info = { 0 };
        {
            GglArena alloc = ggl_arena_init(GGL_BUF(decode_buffer));
            err = gg_uri_parse(&alloc, ggl_obj_into_buf(*uri_obj), &info);
            if (err != GGL_ERR_OK) {
                return err;
            }
        }

        if (ggl_buffer_eq(GGL_STR("docker"), info.scheme)) {
            GglBuffer docker_uri = info.path;
            GglDockerUriInfo docker_info = { 0 };
            err = gg_docker_uri_parse(docker_uri, &docker_info);
            if (err != GGL_ERR_OK) {
                GGL_LOGE(
                    "Failed to parse docker URI \"%.*s\"",
                    (int) docker_uri.len,
                    docker_uri.data
                );
                return err;
            }

            if (((docker_info.tag.len == 0) && (docker_info.digest.len == 0))
                || ggl_buffer_eq(docker_info.tag, GGL_STR("latest"))) {
                GGL_LOGD("Latest tag requested. Pulling image.");
            } else if (ggl_docker_check_image(docker_uri) != GGL_ERR_OK) {
                GGL_LOGD("Image not found. Pulling image.");
            } else {
                GGL_LOGD("Image already found, skipping.");
                continue;
            }

            if (!ecr_logged_in) {
                if (ggl_docker_is_uri_private_ecr(docker_info)) {
                    err = ggl_docker_credentials_ecr_retrieve(
                        docker_info, sigv4_from_tes(tes_creds, GGL_STR("ecr"))
                    );
                    if (err != GGL_ERR_OK) {
                        return GGL_ERR_FAILURE;
                    }
                    ecr_logged_in = true;
                }
            }

            err = ggl_docker_pull(docker_uri);
            if (err != GGL_ERR_OK) {
                return GGL_ERR_FAILURE;
            }
            // Docker performs all other necessary checks.
            continue;
        }

        bool needs_verification = false;
        GglBuffer expected_digest;
        if (expected_digest_obj != NULL) {
            expected_digest = ggl_obj_into_buf(*expected_digest_obj);

            if (algorithm != NULL) {
                if (!ggl_buffer_eq(
                        ggl_obj_into_buf(*algorithm), GGL_STR("SHA-256")
                    )) {
                    GGL_LOGE("Unsupported digest algorithm");
                    return GGL_ERR_UNSUPPORTED;
                }
            } else {
                GGL_LOGW("Assuming SHA-256 digest.");
            }

            if (!ggl_base64_decode_in_place(&expected_digest)) {
                GGL_LOGE("Failed to decode digest.");
                return GGL_ERR_PARSE;
            }
            needs_verification = true;
        }

        bool needs_unarchive = false;
        if (unarchive_obj != NULL) {
            err = get_artifact_unarchive_type(
                ggl_obj_into_buf(*unarchive_obj), &needs_unarchive
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
        if (err != GGL_ERR_OK) {
            GGL_LOGE("Failed to create artifact file for write.");
            return err;
        }
        GGL_CLEANUP(cleanup_close, artifact_fd);

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

        err = ggl_fsync(artifact_fd);
        if (err != GGL_ERR_OK) {
            GGL_LOGE("Artifact fsync failed.");
            return err;
        }

        // verify SHA256 digest
        if (needs_verification) {
            GGL_LOGD("Verifying artifact digest");
            err = ggl_verify_sha256_digest(
                component_store_fd, info.file, expected_digest, digest_context
            );
            if (err != GGL_ERR_OK) {
                return err;
            }
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
    GglByteVec *body_vec,
    GglArena *alloc
) {
    GglObject architecture_detail_read_value;
    GglError ret = ggl_gg_config_read(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("configuration"),
            GGL_STR("platformOverride"),
            GGL_STR("architecture.detail")
        ),
        alloc,
        &architecture_detail_read_value
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGD("No architecture.detail found, so not including it in the "
                 "component candidates search.");
        architecture_detail_read_value = ggl_obj_buf(GGL_STR(""));
    }

    if (ggl_obj_type(architecture_detail_read_value) != GGL_TYPE_BUF) {
        GGL_LOGD(
            "architecture.detail platformOverride in the config is not a "
            "buffer, so not including it in the component candidates search"
        );
        architecture_detail_read_value = ggl_obj_buf(GGL_STR(""));
    }

    // TODO: Support platform attributes for platformOverride configuration
    GglMap platform_attributes = GGL_MAP(
        ggl_kv(GGL_STR("runtime"), ggl_obj_buf(GGL_STR("aws_nucleus_lite"))),
        ggl_kv(GGL_STR("os"), ggl_obj_buf(GGL_STR("linux"))),
        ggl_kv(
            GGL_STR("architecture"), ggl_obj_buf(get_current_architecture())
        ),
        ggl_kv(GGL_STR("architecture.detail"), architecture_detail_read_value)
    );

    if (ggl_obj_into_buf(architecture_detail_read_value).len == 0) {
        platform_attributes.len -= 1;
    }

    GglMap platform_info = GGL_MAP(
        ggl_kv(GGL_STR("name"), ggl_obj_buf(GGL_STR("linux"))),
        ggl_kv(GGL_STR("attributes"), ggl_obj_map(platform_attributes))
    );

    GglMap version_requirements_map = GGL_MAP(
        ggl_kv(GGL_STR("requirements"), ggl_obj_buf(component_requirements))
    );

    GglMap component_map = GGL_MAP(
        ggl_kv(GGL_STR("componentName"), ggl_obj_buf(component_name)),
        ggl_kv(
            GGL_STR("versionRequirements"),
            ggl_obj_map(version_requirements_map)
        )
    );

    GglList candidates_list = GGL_LIST(ggl_obj_map(component_map));

    GglMap request_body = GGL_MAP(
        ggl_kv(GGL_STR("componentCandidates"), ggl_obj_list(candidates_list)),
        ggl_kv(GGL_STR("platform"), ggl_obj_map(platform_info))
    );

    ret = ggl_json_encode(
        ggl_obj_map(request_body), priv_byte_vec_writer(body_vec)
    );
    ggl_byte_vec_chain_push(&ret, body_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error while encoding body for ResolveComponentCandidates call"
        );
        return ret;
    }

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
    static uint8_t rcc_body_config_read_mem[128];
    GglArena rcc_alloc = ggl_arena_init(GGL_BUF(rcc_body_config_read_mem));
    GglError ret = generate_resolve_component_candidates_body(
        component_name, version_requirements, &body_vec, &rcc_alloc
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
    GglArena alloc = ggl_arena_init(GGL_BUF(candidates_response_mem));
    GglError ret = ggl_json_decode_destructive(
        dataplane_response, &alloc, &json_candidates_response_obj
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error when parsing resolveComponentCandidates response to "
                 "json.");
        return ret;
    }

    if (ggl_obj_type(json_candidates_response_obj) != GGL_TYPE_MAP) {
        GGL_LOGE("resolveComponentCandidates response did not parse into a "
                 "map.");
        return ret;
    }

    GglObject *resolved_component_versions;
    if (!ggl_map_get(
            ggl_obj_into_map(json_candidates_response_obj),
            GGL_STR("resolvedComponentVersions"),
            &resolved_component_versions
        )) {
        GGL_LOGE("Missing resolvedComponentVersions.");
        return ret;
    }
    if (ggl_obj_type(*resolved_component_versions) != GGL_TYPE_LIST) {
        GGL_LOGE("resolvedComponentVersions response is not a list.");
        return ret;
    }

    bool first_component = true;
    GGL_LIST_FOREACH (
        resolved_version, ggl_obj_into_list(*resolved_component_versions)
    ) {
        if (!first_component) {
            GGL_LOGE(
                "resolveComponentCandidates returned information for more than "
                "one component."
            );
            return GGL_ERR_INVALID;
        }
        first_component = false;

        if (ggl_obj_type(*resolved_version) != GGL_TYPE_MAP) {
            GGL_LOGE("Resolved version is not of type map.");
            return ret;
        }

        GglObject *cloud_component_arn_obj;
        GglObject *cloud_component_name_obj;
        GglObject *cloud_component_version_obj;
        GglObject *vendor_guidance_obj;
        GglObject *recipe_obj;

        ret = ggl_map_validate(
            ggl_obj_into_map(*resolved_version),
            GGL_MAP_SCHEMA(
                { GGL_STR("arn"),
                  GGL_REQUIRED,
                  GGL_TYPE_BUF,
                  &cloud_component_arn_obj },
                { GGL_STR("componentName"),
                  GGL_REQUIRED,
                  GGL_TYPE_BUF,
                  &cloud_component_name_obj },
                { GGL_STR("componentVersion"),
                  GGL_REQUIRED,
                  GGL_TYPE_BUF,
                  &cloud_component_version_obj },
                { GGL_STR("vendorGuidance"),
                  GGL_OPTIONAL,
                  GGL_TYPE_BUF,
                  &vendor_guidance_obj },
                { GGL_STR("recipe"), GGL_REQUIRED, GGL_TYPE_BUF, &recipe_obj },
            )
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        GglBuffer cloud_component_arn
            = ggl_obj_into_buf(*cloud_component_arn_obj);
        GglBuffer cloud_component_name
            = ggl_obj_into_buf(*cloud_component_name_obj);
        GglBuffer cloud_component_version
            = ggl_obj_into_buf(*cloud_component_version_obj);
        GglBuffer recipe_file_content = ggl_obj_into_buf(*recipe_obj);

        assert(cloud_component_version.len <= NAME_MAX);

        memcpy(
            cloud_version->data,
            cloud_component_version.data,
            cloud_component_version.len
        );
        cloud_version->len = cloud_component_version.len;

        if (vendor_guidance_obj != NULL) {
            if (ggl_buffer_eq(
                    ggl_obj_into_buf(*vendor_guidance_obj),
                    GGL_STR("DISCONTINUED")
                )) {
                GGL_LOGW("The component version has been discontinued by its "
                         "publisher. You can deploy this component version, "
                         "but we recommend that you use a different version of "
                         "this component");
            }
        }

        if (recipe_file_content.len == 0) {
            GGL_LOGE("Recipe is empty.");
        }

        bool decoded = ggl_base64_decode_in_place(&recipe_file_content);
        if (!decoded) {
            GGL_LOGE("Failed to decode recipe base64.");
            return GGL_ERR_PARSE;
        }
        recipe_file_content.data[recipe_file_content.len] = '\0';

        GGL_LOGD(
            "Decoded recipe data as: %.*s",
            (int) recipe_file_content.len,
            recipe_file_content.data
        );

        static uint8_t recipe_name_buf[PATH_MAX];
        GglByteVec recipe_name_vec = GGL_BYTE_VEC(recipe_name_buf);
        ret = ggl_byte_vec_append(&recipe_name_vec, cloud_component_name);
        ggl_byte_vec_chain_append(&ret, &recipe_name_vec, GGL_STR("-"));
        ggl_byte_vec_chain_append(
            &ret, &recipe_name_vec, cloud_component_version
        );
        ggl_byte_vec_chain_append(&ret, &recipe_name_vec, GGL_STR(".json"));
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

        {
            // Write file
            int root_dir_fd = -1;
            ret = ggl_dir_open(recipe_dir_vec.buf, O_PATH, true, &root_dir_fd);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to open dir when writing cloud recipe.");
                return ret;
            }
            GGL_CLEANUP(cleanup_close, root_dir_fd);

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
            GGL_CLEANUP(cleanup_close, fd);

            ret = ggl_file_write(fd, recipe_file_content);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Write to cloud recipe file failed");
                return ret;
            }
        }

        GGL_LOGD("Saved recipe under the name %s", recipe_name_vec.buf.data);

        ret = ggl_gg_config_write(
            GGL_BUF_LIST(GGL_STR("services"), cloud_component_name, ),
            ggl_obj_map(GGL_MAP(
                ggl_kv(GGL_STR("arn"), ggl_obj_buf(cloud_component_arn))
            )),
            &(int64_t) { 1 }
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Write of arn to config failed");
            return ret;
        }
    }

    return GGL_ERR_OK;
}

static GglError parse_thing_groups_list(
    GglBuffer list_thing_groups_response,
    GglArena *alloc,
    GglObject **thing_groups_list
) {
    // TODO: Add a schema and only parse the fields we need to save memory
    GglObject json_thing_groups_object;
    GglError ret = ggl_json_decode_destructive(
        list_thing_groups_response, alloc, &json_thing_groups_object
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error when parsing listThingGroups response to "
                 "json.");
        return ret;
    }

    if (ggl_obj_type(json_thing_groups_object) != GGL_TYPE_MAP) {
        GGL_LOGE("listThingGroups response did not parse into a "
                 "map.");
        return ret;
    }

    if (!ggl_map_get(
            ggl_obj_into_map(json_thing_groups_object),
            GGL_STR("thingGroups"),
            thing_groups_list
        )) {
        GGL_LOGE("Missing thingGroups.");
        return ret;
    }
    if (ggl_obj_type(**thing_groups_list) != GGL_TYPE_LIST) {
        GGL_LOGE("thingGroups response is not a list.");
        return ret;
    }

    return GGL_ERR_OK;
}

static GglError add_thing_groups_list_to_config(GglObject *thing_groups_list) {
    GglError ret = ggl_gg_config_write(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("DeploymentService"),
            GGL_STR("lastThingGroupsListFromCloud")
        ),
        *thing_groups_list,
        &(int64_t) { 1 }
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Write of lastThingGroupsListFromCloud to config failed");
        return ret;
    }

    return GGL_ERR_OK;
}

static GglError resolve_dependencies(
    GglMap root_components,
    GglBuffer thing_group_name,
    GglDeploymentHandlerThreadArgs *args,
    GglArena *alloc,
    GglKVVec *resolved_components_kv_vec
) {
    GglError ret;

    // TODO: Decide on size
    GglKVVec components_to_resolve = GGL_KV_VEC((GglKV[64]) { 0 });

    static uint8_t version_requirements_mem[2048] = { 0 };
    GglArena version_requirements_alloc
        = ggl_arena_init(GGL_BUF(version_requirements_mem));

    // Root components from current deployment
    GGL_MAP_FOREACH (pair, root_components) {
        if (ggl_obj_type(*ggl_kv_val(pair)) != GGL_TYPE_MAP) {
            GGL_LOGE("Incorrect formatting for deployment components field.");
            return GGL_ERR_INVALID;
        }

        GglObject *val;
        GglBuffer component_version = { 0 };
        if (ggl_map_get(
                ggl_obj_into_map(*ggl_kv_val(pair)), GGL_STR("version"), &val
            )) {
            if (ggl_obj_type(*val) != GGL_TYPE_BUF) {
                GGL_LOGE("Received invalid argument.");
                return GGL_ERR_INVALID;
            }
            component_version = ggl_obj_into_buf(*val);
        }

        if (ggl_buffer_eq(
                ggl_kv_key(*pair), GGL_STR("aws.greengrass.NucleusLite")
            )) {
            GglBuffer software_version = GGL_STR(GGL_VERSION);
            if (!ggl_buffer_eq(component_version, software_version)) {
                GGL_LOGE(
                    "The deployment failed. The aws.greengrass.NucleusLite "
                    "component version specified in the deployment is %.*s, "
                    "but the version of the GG Lite software is %.*s. Please "
                    "ensure that the version in the deployment matches before "
                    "attempting the deployment again.",
                    (int) component_version.len,
                    component_version.data,
                    (int) software_version.len,
                    software_version.data
                );
                return GGL_ERR_INVALID;
            }
        }

        ret = ggl_kv_vec_push(
            &components_to_resolve,
            ggl_kv(ggl_kv_key(*pair), ggl_obj_buf(component_version))
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    // At this point, components_to_resolve should be only a map of root
    // component names to their version requirements from the deployment. This
    // may be empty! We delete the key first in case components were removed.
    ret = ggl_gg_config_delete(GGL_BUF_LIST(
        GGL_STR("services"),
        GGL_STR("DeploymentService"),
        GGL_STR("thingGroupsToRootComponents"),
        thing_group_name
    ));

    if (ret != GGL_ERR_OK) {
        GGL_LOGW(
            "Error while deleting thing group to root components mapping for "
            "thing group %.*s",
            (int) thing_group_name.len,
            thing_group_name.data
        );
        return ret;
    }
    ret = ggl_gg_config_write(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("DeploymentService"),
            GGL_STR("thingGroupsToRootComponents"),
            thing_group_name
        ),
        ggl_obj_map(components_to_resolve.map),
        0
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Failed to write thing group to root components map to ggconfigd."
        );
        return ret;
    }

    // Get list of thing groups
    static uint8_t list_thing_groups_response_buf[2048] = { 0 };
    GglBuffer list_thing_groups_response
        = GGL_BUF(list_thing_groups_response_buf);

    GglObject *thing_groups_list = NULL;
    GglObject empty_list_obj = ggl_obj_list(GGL_LIST());
    uint8_t thing_groups_response_mem[100 * sizeof(GglObject)];
    GglArena thing_groups_json_alloc
        = ggl_arena_init(GGL_BUF(thing_groups_response_mem));

    // TODO: Retry infinitely for cloud deployment
    ret = get_device_thing_groups(&list_thing_groups_response);
    if (ret == GGL_ERR_OK) {
        ret = parse_thing_groups_list(
            list_thing_groups_response,
            &thing_groups_json_alloc,
            &thing_groups_list
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Error when parsing listThingGroups response for thing groups"
            );
            return ret;
        }
        ret = add_thing_groups_list_to_config(thing_groups_list);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Error updating config with the thing groups list");
            return ret;
        }
    } else {
        if (!ggl_buffer_eq(GGL_STR("LOCAL_DEPLOYMENTS"), thing_group_name)) {
            GGL_LOGE("Cloud call to list thing groups failed. Cloud deployment "
                     "requires an updated thing group list.");
            return ret;
        }
        GGL_LOGI("Cloud call to list thing groups failed. Using previous thing "
                 "groups list as deployment is local.");
        ret = ggl_gg_config_read(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("DeploymentService"),
                GGL_STR("lastThingGroupsListFromCloud")
            ),
            alloc,
            thing_groups_list
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGI(
                "No info found in config for thing groups list, assuming no "
                "thing group memberships."
            );
            thing_groups_list = &empty_list_obj;
        }
    }

    GGL_LIST_FOREACH (thing_group_item, ggl_obj_into_list(*thing_groups_list)) {
        if (ggl_obj_type(*thing_group_item) != GGL_TYPE_MAP) {
            GGL_LOGE("Thing group item is not of type map.");
            return ret;
        }

        GglObject *thing_group_name_from_item_obj;

        ret = ggl_map_validate(
            ggl_obj_into_map(*thing_group_item),
            GGL_MAP_SCHEMA(
                { GGL_STR("thingGroupName"),
                  GGL_REQUIRED,
                  GGL_TYPE_BUF,
                  &thing_group_name_from_item_obj },
            )
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        GglBuffer thing_group_name_from_item
            = ggl_obj_into_buf(*thing_group_name_from_item_obj);

        if (!ggl_buffer_eq(thing_group_name_from_item, thing_group_name)) {
            GglObject group_root_components_read_value;
            ret = ggl_gg_config_read(
                GGL_BUF_LIST(
                    GGL_STR("services"),
                    GGL_STR("DeploymentService"),
                    GGL_STR("thingGroupsToRootComponents"),
                    thing_group_name_from_item
                ),
                alloc,
                &group_root_components_read_value
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGI(
                    "No info found in config for root components for thing "
                    "group %.*s, assuming no components are part of this thing "
                    "group.",
                    (int) thing_group_name_from_item.len,
                    thing_group_name_from_item.data
                );
            } else {
                if (ggl_obj_type(group_root_components_read_value)
                    != GGL_TYPE_MAP) {
                    GGL_LOGE(
                        "Did not read a map from config for thing group to "
                        "root components map"
                    );
                    return GGL_ERR_INVALID;
                }

                GGL_MAP_FOREACH (
                    root_component_pair,
                    ggl_obj_into_map(group_root_components_read_value)
                ) {
                    GglBuffer root_component_val
                        = ggl_obj_into_buf(*ggl_kv_val(root_component_pair));

                    // If component is already in the root component list, it
                    // must be the same version as the one already in the list
                    // or we have a conflict.
                    GglObject *existing_root_component_version_obj;
                    ret = ggl_map_validate(
                        components_to_resolve.map,
                        GGL_MAP_SCHEMA(
                            { ggl_kv_key(*root_component_pair),
                              GGL_OPTIONAL,
                              GGL_TYPE_BUF,
                              &existing_root_component_version_obj },
                        )
                    );
                    if (ret != GGL_ERR_OK) {
                        return ret;
                    }

                    bool need_to_add_root_component = true;

                    if (existing_root_component_version_obj != NULL) {
                        GglBuffer existing_root_component_version
                            = ggl_obj_into_buf(
                                *existing_root_component_version_obj
                            );
                        if (ggl_buffer_eq(
                                existing_root_component_version,
                                ggl_obj_into_buf(*ggl_kv_val(root_component_pair
                                ))
                            )) {
                            need_to_add_root_component = false;
                        } else {
                            GGL_LOGE(
                                "There is a version conflict for component "
                                "%.*s, where two deployments are asking for "
                                "versions %.*s and %.*s. Please check that "
                                "this root component does not have conflicting "
                                "versions across your deployments.",
                                (int) ggl_kv_key(*root_component_pair).len,
                                ggl_kv_key(*root_component_pair).data,
                                (int) root_component_val.len,
                                root_component_val.data,
                                (int) existing_root_component_version.len,
                                existing_root_component_version.data
                            );
                            return GGL_ERR_INVALID;
                        }
                    }

                    if (need_to_add_root_component) {
                        GglBuffer root_component_name_buf
                            = ggl_kv_key(*root_component_pair);
                        ret = ggl_arena_claim_buf(
                            &root_component_name_buf, alloc
                        );
                        if (ret != GGL_ERR_OK) {
                            return ret;
                        }

                        GglBuffer root_component_version_buf
                            = root_component_val;
                        ret = ggl_arena_claim_buf(
                            &root_component_version_buf,
                            &version_requirements_alloc
                        );
                        if (ret != GGL_ERR_OK) {
                            return ret;
                        }

                        ret = ggl_kv_vec_push(
                            &components_to_resolve,
                            ggl_kv(
                                root_component_name_buf,
                                ggl_obj_buf(root_component_version_buf)
                            )
                        );
                        if (ret != GGL_ERR_OK) {
                            return ret;
                        }

                        GGL_LOGD(
                            "Added %.*s to the list of root components to "
                            "resolve "
                            "from "
                            "the thing group %.*s",
                            (int) root_component_name_buf.len,
                            root_component_name_buf.data,
                            (int) thing_group_name_from_item.len,
                            thing_group_name_from_item.data
                        );
                    }
                }
            }
        }
    }

    // Add local components to components to resolve, if it isn't a local
    // deployment
    if (!ggl_buffer_eq(GGL_STR("LOCAL_DEPLOYMENTS"), thing_group_name)) {
        GglObject local_components_read_value;
        ret = ggl_gg_config_read(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("DeploymentService"),
                GGL_STR("thingGroupsToRootComponents"),
                GGL_STR("LOCAL_DEPLOYMENTS")
            ),
            alloc,
            &local_components_read_value
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGI("No local components found in config, proceeding "
                     "deployment without needing to add local components.");
        } else {
            if (ggl_obj_type(local_components_read_value) != GGL_TYPE_MAP) {
                GGL_LOGE("Did not read a map from config while looking up "
                         "local components.");
                return GGL_ERR_INVALID;
            }

            GGL_MAP_FOREACH (
                root_component_pair,
                ggl_obj_into_map(local_components_read_value)
            ) {
                GglBuffer root_component_val
                    = ggl_obj_into_buf(*ggl_kv_val(root_component_pair));

                // If component is already in the root component list, it
                // must be the same version as the one already in the list
                // or we have a conflict.
                GglObject *existing_root_component_version_obj;
                ret = ggl_map_validate(
                    components_to_resolve.map,
                    GGL_MAP_SCHEMA(
                        { ggl_kv_key(*root_component_pair),
                          GGL_OPTIONAL,
                          GGL_TYPE_BUF,
                          &existing_root_component_version_obj },
                    )
                );
                if (ret != GGL_ERR_OK) {
                    return ret;
                }

                bool need_to_add_root_component = true;

                if (existing_root_component_version_obj != NULL) {
                    GglBuffer existing_root_component_version
                        = ggl_obj_into_buf(*existing_root_component_version_obj
                        );
                    if (ggl_buffer_eq(
                            existing_root_component_version, root_component_val
                        )) {
                        need_to_add_root_component = false;
                    } else {
                        GGL_LOGE(
                            "There is a version conflict for component %.*s, "
                            "where it is already locally deployed as version "
                            "%.*s and the deployment requests version %.*s.",
                            (int) ggl_kv_key(*root_component_pair).len,
                            ggl_kv_key(*root_component_pair).data,
                            (int) root_component_val.len,
                            root_component_val.data,
                            (int) existing_root_component_version.len,
                            existing_root_component_version.data
                        );
                        return GGL_ERR_INVALID;
                    }
                }

                if (need_to_add_root_component) {
                    GglBuffer root_component_name_buf
                        = ggl_kv_key(*root_component_pair);
                    ret = ggl_arena_claim_buf(&root_component_name_buf, alloc);
                    if (ret != GGL_ERR_OK) {
                        return ret;
                    }

                    GglBuffer root_component_version_buf = root_component_val;
                    ret = ggl_arena_claim_buf(
                        &root_component_version_buf, &version_requirements_alloc
                    );
                    if (ret != GGL_ERR_OK) {
                        return ret;
                    }

                    ret = ggl_kv_vec_push(
                        &components_to_resolve,
                        ggl_kv(
                            root_component_name_buf,
                            ggl_obj_buf(root_component_version_buf)
                        )
                    );
                    GGL_LOGD(
                        "Added %.*s to the list of root components to resolve "
                        "as it has been previously locally deployed.",
                        (int) root_component_name_buf.len,
                        root_component_name_buf.data
                    );
                }
            }
        }
    }

    GGL_MAP_FOREACH (pair, components_to_resolve.map) {
        GglBuffer pair_val = ggl_obj_into_buf(*ggl_kv_val(pair));

        // We assume that we have not resolved a component yet if we are finding
        // it in this map.
        uint8_t resolved_version_arr[NAME_MAX];
        GglBuffer resolved_version = GGL_BUF(resolved_version_arr);
        bool found_local_candidate = resolve_component_version(
            ggl_kv_key(*pair), pair_val, &resolved_version
        );

        if (!found_local_candidate) {
            // Resolve with cloud and download recipe
            static uint8_t resolve_component_candidates_response_buf[16384]
                = { 0 };
            GglBuffer resolve_component_candidates_response
                = GGL_BUF(resolve_component_candidates_response_buf);

            ret = resolve_component_with_cloud(
                ggl_kv_key(*pair),
                pair_val,
                &resolve_component_candidates_response
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
                    (int) ggl_kv_key(*pair).len,
                    pair_val.data
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
        ret = ggl_arena_claim_buf(&resolved_version, alloc);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        ret = ggl_kv_vec_push(
            resolved_components_kv_vec,
            ggl_kv(ggl_kv_key(*pair), ggl_obj_buf(resolved_version))
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
        static uint8_t recipe_mem[GGL_COMPONENT_RECIPE_MAX_LEN] = { 0 };
        GglArena recipe_alloc = ggl_arena_init(GGL_BUF(recipe_mem));
        ret = ggl_recipe_get_from_file(
            args->root_path_fd,
            ggl_kv_key(*pair),
            resolved_version,
            &recipe_alloc,
            &recipe_obj
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        GglObject *component_dependencies;

        if (ggl_obj_type(recipe_obj) != GGL_TYPE_MAP) {
            GGL_LOGE("Recipe object did not parse into a map.");
            return GGL_ERR_INVALID;
        }

        ret = ggl_map_validate(
            ggl_obj_into_map(recipe_obj),
            GGL_MAP_SCHEMA(
                { GGL_STR("ComponentDependencies"),
                  GGL_OPTIONAL,
                  GGL_TYPE_MAP,
                  &component_dependencies },
            )
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        if (component_dependencies != NULL) {
            GGL_MAP_FOREACH (
                dependency, ggl_obj_into_map(*component_dependencies)
            ) {
                if (ggl_obj_type(*ggl_kv_val(dependency)) != GGL_TYPE_MAP) {
                    GGL_LOGE(
                        "Component dependency in recipe does not have map data"
                    );
                    return GGL_ERR_INVALID;
                }

                // If the component is aws.greengrass.Nucleus or
                // aws.greengrass.TokenExchangeService ignore it and never add
                // it as a dependency to check or parse.
                if (ggl_buffer_eq(
                        ggl_kv_key(*dependency),
                        GGL_STR("aws.greengrass.Nucleus")
                    )
                    || ggl_buffer_eq(
                        ggl_kv_key(*dependency),
                        GGL_STR("aws.greengrass.TokenExchangeService")
                    )) {
                    GGL_LOGD(
                        "Skipping a dependency during resolution as it is %.*s",
                        (int) ggl_kv_key(*dependency).len,
                        ggl_kv_key(*dependency).data
                    );
                    continue;
                }

                GglObject *dep_version_requirement_obj = NULL;
                ret = ggl_map_validate(
                    ggl_obj_into_map(*ggl_kv_val(dependency)),
                    GGL_MAP_SCHEMA(
                        { GGL_STR("VersionRequirement"),
                          GGL_REQUIRED,
                          GGL_TYPE_BUF,
                          &dep_version_requirement_obj },
                    )
                );
                if (ret != GGL_ERR_OK) {
                    return ret;
                }
                GglBuffer dep_version_requirement
                    = ggl_obj_into_buf(*dep_version_requirement_obj);

                // If we already resolved the component version, check that it
                // still satisfies the new requirement and fail otherwise.
                GglObject *already_resolved_version;
                ret = ggl_map_validate(
                    resolved_components_kv_vec->map,
                    GGL_MAP_SCHEMA(
                        { ggl_kv_key(*dependency),
                          GGL_OPTIONAL,
                          GGL_TYPE_BUF,
                          &already_resolved_version },
                    )
                );
                if (ret != GGL_ERR_OK) {
                    return ret;
                }
                if (already_resolved_version != NULL) {
                    bool meets_requirements = is_in_range(
                        ggl_obj_into_buf(*already_resolved_version),
                        dep_version_requirement
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
                            { ggl_kv_key(*dependency),
                              GGL_OPTIONAL,
                              GGL_TYPE_BUF,
                              &existing_requirements },
                        )
                    );
                    if (ret != GGL_ERR_OK) {
                        return ret;
                    }
                    if (existing_requirements != NULL) {
                        uint8_t new_req_buf[PATH_MAX];
                        GglByteVec new_req_vec = GGL_BYTE_VEC(new_req_buf);
                        ret = ggl_byte_vec_append(
                            &new_req_vec,
                            ggl_obj_into_buf(*existing_requirements)
                        );
                        ggl_byte_vec_chain_push(&ret, &new_req_vec, ' ');
                        ggl_byte_vec_chain_append(
                            &ret, &new_req_vec, dep_version_requirement
                        );
                        if (ret != GGL_ERR_OK) {
                            GGL_LOGE("Failed to create new requirements for "
                                     "dependency version.");
                            return ret;
                        }

                        uint8_t *new_req = GGL_ARENA_ALLOCN(
                            &version_requirements_alloc,
                            uint8_t,
                            new_req_vec.buf.len
                        );
                        if (new_req == NULL) {
                            GGL_LOGE("Ran out of memory while trying to create "
                                     "new requirements");
                            return GGL_ERR_NOMEM;
                        }

                        memcpy(
                            new_req, new_req_vec.buf.data, new_req_vec.buf.len
                        );
                        *existing_requirements = ggl_obj_buf((GglBuffer
                        ) { .data = new_req, .len = new_req_vec.buf.len });
                    }

                    // If we haven't resolved it yet, and it doesn't have an
                    // existing requirement, add it.
                    if (!existing_requirements) {
                        GglBuffer name_key_buf = ggl_kv_key(*dependency);
                        ret = ggl_arena_claim_buf(&name_key_buf, alloc);
                        if (ret != GGL_ERR_OK) {
                            return ret;
                        }

                        GglBuffer vers_key_buf = dep_version_requirement;
                        ret = ggl_arena_claim_buf(
                            &vers_key_buf, &version_requirements_alloc
                        );
                        if (ret != GGL_ERR_OK) {
                            return ret;
                        }

                        ret = ggl_kv_vec_push(
                            &components_to_resolve,
                            ggl_kv(name_key_buf, ggl_obj_buf(vers_key_buf))
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
    GGL_CLEANUP(cleanup_close, component_fd);
    return ggl_dir_openat(
        component_fd, component_version, O_PATH, true, version_fd
    );
}

static GglBuffer get_unversioned_substring(GglBuffer arn) {
    size_t colon_index = SIZE_MAX;
    for (size_t i = arn.len; i > 0; i--) {
        if (arn.data[i - 1] == ':') {
            colon_index = i - 1;
            break;
        }
    }
    return ggl_buffer_substr(arn, 0, colon_index);
}

static GglError add_arn_list_to_config(
    GglBuffer component_name, GglBuffer configuration_arn
) {
    GGL_LOGD(
        "Writing %.*s to %.*s/configArn",
        (int) configuration_arn.len,
        configuration_arn.data,
        (int) component_name.len,
        component_name.data
    );

    // add configuration arn to the config if it is not already present
    // added to the config as a list, this is later used in fss

    // TODO: local deployments should be represented by one deployment target,
    // rather than each having their own unique deploymentId as a target. This
    // can be done where the local deployment cli handler is responsible for
    // mutating the local deployment before sending the updated local deployment
    // info to this deployment handler.
    static uint8_t arn_list_mem
        [((size_t) DEPLOYMENT_TARGET_NAME_MAX_CHARS * MAX_DEPLOYMENT_TARGETS)
         + (sizeof(GglObject) * MAX_DEPLOYMENT_TARGETS)];
    GglArena arn_list_alloc = ggl_arena_init(GGL_BUF(arn_list_mem));

    GglObject arn_list_obj;
    GglError ret = ggl_gg_config_read(
        GGL_BUF_LIST(GGL_STR("services"), component_name, GGL_STR("configArn")),
        &arn_list_alloc,
        &arn_list_obj
    );

    if ((ret != GGL_ERR_OK) && (ret != GGL_ERR_NOENTRY)) {
        GGL_LOGE("Failed to retrieve configArn.");
        return GGL_ERR_FAILURE;
    }

    GglObjVec new_arn_list
        = GGL_OBJ_VEC((GglObject[MAX_DEPLOYMENT_TARGETS]) { 0 });
    if (ret != GGL_ERR_NOENTRY) {
        // list exists in config, parse for current config arn and append if it
        // is not already included
        if (ggl_obj_type(arn_list_obj) != GGL_TYPE_LIST) {
            GGL_LOGE("Configuration arn list not of expected type.");
            return GGL_ERR_INVALID;
        }

        GglList arn_list = ggl_obj_into_list(arn_list_obj);
        if (arn_list.len >= MAX_DEPLOYMENT_TARGETS) {
            GGL_LOGE(
                "Cannot append configArn: Component is deployed as part of too "
                "many deployments (%zu >= %zu).",
                arn_list.len,
                (size_t) MAX_DEPLOYMENT_TARGETS
            );
            return GGL_ERR_FAILURE;
        }
        GGL_LIST_FOREACH (arn, arn_list) {
            if (ggl_obj_type(*arn) != GGL_TYPE_BUF) {
                GGL_LOGE("Configuration arn not of type buffer.");
                return ret;
            }
            if (ggl_buffer_eq(
                    get_unversioned_substring(ggl_obj_into_buf(*arn)),
                    get_unversioned_substring(configuration_arn)
                )) {
                // arn for this group already added to config, replace it
                GGL_LOGD("Configuration arn already exists for this thing "
                         "group, overwriting it.");
                *arn = ggl_obj_buf(configuration_arn);
                ret = ggl_gg_config_write(
                    GGL_BUF_LIST(
                        GGL_STR("services"),
                        component_name,
                        GGL_STR("configArn")
                    ),
                    ggl_obj_list(arn_list),
                    &(int64_t) { 3 }
                );
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE(
                        "Failed to write configuration arn list to the config."
                    );
                    return ret;
                }
                return GGL_ERR_OK;
            }
            ret = ggl_obj_vec_push(&new_arn_list, *arn);
            assert(ret == GGL_ERR_OK);
        }
    }

    ret = ggl_obj_vec_push(&new_arn_list, ggl_obj_buf(configuration_arn));
    assert(ret == GGL_ERR_OK);

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(GGL_STR("services"), component_name, GGL_STR("configArn")),
        ggl_obj_list(new_arn_list.list),
        &(int64_t) { 3 }
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to write configuration arn list to the config.");
        return ret;
    }

    return GGL_ERR_OK;
}

static GglError send_fss_update(
    GglDeployment *deployment, bool deployment_succeeded
) {
    GglBuffer server = GGL_STR("gg_fleet_status");
    static uint8_t buffer[10 * sizeof(GglObject)] = { 0 };

    // TODO: Fill out statusDetails and unchangedRootComponents
    GglMap status_details_map = GGL_MAP(
        ggl_kv(
            GGL_STR("detailedStatus"),
            ggl_obj_buf(
                deployment_succeeded ? GGL_STR("SUCCESSFUL")
                                     : GGL_STR("FAILED_ROLLBACK_NOT_REQUESTED")
            )
        ),
    );

    GglMap deployment_info = GGL_MAP(
        ggl_kv(
            GGL_STR("status"),
            ggl_obj_buf(
                deployment_succeeded ? GGL_STR("SUCCEEDED") : GGL_STR("FAILED")
            )
        ),
        ggl_kv(
            GGL_STR("fleetConfigurationArnForStatus"),
            ggl_obj_buf(deployment->configuration_arn)
        ),
        ggl_kv(GGL_STR("deploymentId"), ggl_obj_buf(deployment->deployment_id)),
        ggl_kv(GGL_STR("statusDetails"), ggl_obj_map(status_details_map)),
        ggl_kv(GGL_STR("unchangedRootComponents"), ggl_obj_list(GGL_LIST())),
    );

    uint8_t trigger_buffer[24];
    GglBuffer trigger = GGL_BUF(trigger_buffer);

    if (deployment->type == LOCAL_DEPLOYMENT) {
        trigger = GGL_STR("LOCAL_DEPLOYMENT");
    } else if (deployment->type == THING_GROUP_DEPLOYMENT) {
        trigger = GGL_STR("THING_GROUP_DEPLOYMENT");
    }

    GglMap args = GGL_MAP(
        ggl_kv(GGL_STR("trigger"), ggl_obj_buf(trigger)),
        ggl_kv(GGL_STR("deployment_info"), ggl_obj_map(deployment_info))
    );

    GglArena alloc = ggl_arena_init(GGL_BUF(buffer));
    GglObject result;

    GglError ret = ggl_call(
        server, GGL_STR("send_fleet_status_update"), args, NULL, &alloc, &result
    );

    if (ret != 0) {
        GGL_LOGE(
            "Failed to send send_fleet_status_update to fleet status service: "
            "%d.",
            ret
        );
        return ret;
    }

    return GGL_ERR_OK;
}

static GglError deployment_status_callback(void *ctx, GglObject data) {
    (void) ctx;
    if (ggl_obj_type(data) != GGL_TYPE_MAP) {
        GGL_LOGE("Result is not a map.");
        return GGL_ERR_INVALID;
    }
    GglObject *component_name_obj;
    GglObject *status_obj;
    GglError ret = ggl_map_validate(
        ggl_obj_into_map(data),
        GGL_MAP_SCHEMA(
            { GGL_STR("component_name"),
              GGL_REQUIRED,
              GGL_TYPE_BUF,
              &component_name_obj },
            { GGL_STR("lifecycle_state"),
              GGL_REQUIRED,
              GGL_TYPE_BUF,
              &status_obj }
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Unexpected gghealthd response format.");
        return GGL_ERR_INVALID;
    }
    GglBuffer component_name = ggl_obj_into_buf(*component_name_obj);
    GglBuffer status = ggl_obj_into_buf(*status_obj);

    if (ggl_buffer_eq(status, GGL_STR("BROKEN"))) {
        GGL_LOGE(
            "%.*s is broken.", (int) component_name.len, component_name.data
        );
        return GGL_ERR_FAILURE;
    }
    if (ggl_buffer_eq(status, GGL_STR("RUNNING"))
        || ggl_buffer_eq(status, GGL_STR("FINISHED"))) {
        GGL_LOGD("Component succeeded.");
        return GGL_ERR_OK;
    }
    GGL_LOGE("Unexpected lifecycle state %.*s", (int) status.len, status.data);
    return GGL_ERR_INVALID;
}

static GglError wait_for_phase_status(
    GglBufVec component_vec, GglBuffer phase
) {
    // TODO: hack
    (void) ggl_sleep(5);

    for (size_t i = 0; i < component_vec.buf_list.len; i++) {
        // Add .[phase name] into the component name
        static uint8_t full_comp_name_mem[PATH_MAX];
        GglByteVec full_comp_name_vec = GGL_BYTE_VEC(full_comp_name_mem);
        GglError ret = ggl_byte_vec_append(
            &full_comp_name_vec, component_vec.buf_list.bufs[i]
        );
        ggl_byte_vec_chain_push(&ret, &full_comp_name_vec, '.');
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to push '.' character to component name vector.");
            return ret;
        }
        ret = ggl_byte_vec_append(&full_comp_name_vec, phase);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to generate %*.s phase name for %*.scomponent.",
                (int) phase.len,
                phase.data,
                (int) component_vec.buf_list.bufs[i].len,
                component_vec.buf_list.bufs[i].data
            );
            return ret;
        }
        GGL_LOGD(
            "Awaiting %.*s to finish.",
            (int) full_comp_name_vec.buf.len,
            full_comp_name_vec.buf.data
        );

        ret = ggl_sub_response(
            GGL_STR("gg_health"),
            GGL_STR("subscribe_to_lifecycle_completion"),
            GGL_MAP(ggl_kv(
                GGL_STR("component_name"), ggl_obj_buf(full_comp_name_vec.buf)
            )),
            deployment_status_callback,
            NULL,
            NULL,
            300
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed waiting for %.*s",
                (int) full_comp_name_vec.buf.len,
                full_comp_name_vec.buf.data
            );
            return GGL_ERR_FAILURE;
        }
    }
    return GGL_ERR_OK;
}

static GglError wait_for_deployment_status(GglMap resolved_components) {
    GGL_LOGT("Beginning wait for deployment completion");
    // TODO: hack
    (void) ggl_sleep(5);

    GGL_MAP_FOREACH (component, resolved_components) {
        GGL_LOGD(
            "Waiting for %.*s to finish",
            (int) ggl_kv_key(*component).len,
            ggl_kv_key(*component).data
        );
        GglError ret = ggl_sub_response(
            GGL_STR("gg_health"),
            GGL_STR("subscribe_to_lifecycle_completion"),
            GGL_MAP(ggl_kv(
                GGL_STR("component_name"), ggl_obj_buf(ggl_kv_key(*component))
            )),
            deployment_status_callback,
            NULL,
            NULL,
            300
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed waiting for %.*s",
                (int) ggl_kv_key(*component).len,
                ggl_kv_key(*component).data
            );
            return GGL_ERR_FAILURE;
        }
    }
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void handle_deployment(
    GglDeployment *deployment,
    GglDeploymentHandlerThreadArgs *args,
    bool *deployment_succeeded
) {
    int root_path_fd = args->root_path_fd;
    if (deployment->recipe_directory_path.len != 0) {
        GglError ret = merge_dir_to(
            deployment->recipe_directory_path, "packages/recipes/"
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to copy recipes.");
            return;
        }
    }

    if (deployment->artifacts_directory_path.len != 0) {
        GglError ret = merge_dir_to(
            deployment->artifacts_directory_path, "packages/artifacts/"
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to copy artifacts.");
            return;
        }
    }

    GglKVVec resolved_components_kv_vec = GGL_KV_VEC((GglKV[64]) { 0 });
    static uint8_t resolve_dependencies_mem[8192] = { 0 };
    GglArena resolve_dependencies_alloc
        = ggl_arena_init(GGL_BUF(resolve_dependencies_mem));
    GglError ret = resolve_dependencies(
        deployment->components,
        deployment->thing_group,
        args,
        &resolve_dependencies_alloc,
        &resolved_components_kv_vec
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to do dependency resolution for deployment, failing "
                 "deployment.");
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
    bool tes_creds_retrieved = (ret == GGL_ERR_OK);
    if (!tes_creds_retrieved) {
        GGL_LOGW("Failed to retrieve TES credentials, attempting to complete "
                 "deployment without TES credentials.");
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

    GglDigest digest_context = ggl_new_digest(&ret);
    if (ret != GGL_ERR_OK) {
        return;
    }
    GGL_CLEANUP(ggl_free_digest, digest_context);

    // list of {component name -> component version} for all new components in
    // the deployment
    GglKVVec components_to_deploy = GGL_KV_VEC((GglKV[64]) { 0 });

    GGL_MAP_FOREACH (pair, resolved_components_kv_vec.map) {
        GglBuffer pair_val = ggl_obj_into_buf(*ggl_kv_val(pair));

        // check config to see if component has completed processing
        GglArena resp_alloc = ggl_arena_init(GGL_BUF((uint8_t[128]) { 0 }));
        GglBuffer resp;

        ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("DeploymentService"),
                GGL_STR("deploymentState"),
                GGL_STR("components"),
                ggl_kv_key(*pair)
            ),
            &resp_alloc,
            &resp
        );
        if (ret == GGL_ERR_OK) {
            GGL_LOGD(
                "Component %.*s completed processing in previous run. Will not "
                "be reprocessed.",
                (int) ggl_kv_key(*pair).len,
                ggl_kv_key(*pair).data
            );
            continue;
        }

        // check config to see if bootstrap steps have already been run for this
        // component
        if (component_bootstrap_phase_completed(ggl_kv_key(*pair))) {
            GGL_LOGD(
                "Bootstrap component %.*s encountered. Bootstrap phase has "
                "already been completed. Adding to list of components to "
                "process to complete any other lifecycle stages.",
                (int) ggl_kv_key(*pair).len,
                ggl_kv_key(*pair).data
            );
            ret = ggl_kv_vec_push(
                &components_to_deploy,
                ggl_kv(ggl_kv_key(*pair), *ggl_kv_val(pair))
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "Failed to add component info for %.*s to deployment "
                    "vector.",
                    (int) ggl_kv_key(*pair).len,
                    ggl_kv_key(*pair).data
                );
                return;
            }
            continue;
        }

        int component_artifacts_fd = -1;
        ret = open_component_artifacts_dir(
            artifact_store_fd,
            ggl_kv_key(*pair),
            pair_val,
            &component_artifacts_fd
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to open artifact directory.");
            return;
        }
        int component_archive_dir_fd = -1;
        ret = open_component_artifacts_dir(
            artifact_archive_fd,
            ggl_kv_key(*pair),
            pair_val,
            &component_archive_dir_fd
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to open unarchived artifacts directory.");
            return;
        }
        GglObject recipe_obj;
        static uint8_t recipe_mem[GGL_COMPONENT_RECIPE_MAX_LEN] = { 0 };
        GglArena alloc = ggl_arena_init(GGL_BUF(recipe_mem));
        ret = ggl_recipe_get_from_file(
            args->root_path_fd, ggl_kv_key(*pair), pair_val, &alloc, &recipe_obj
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to validate and decode recipe");
            return;
        }

        // TODO: See if there is a better requirement. If a customer has the
        // same version as before but somehow updated their component
        // version their component may not get the updates.
        bool component_updated = true;

        static uint8_t old_component_version_mem[128] = { 0 };
        alloc = ggl_arena_init(GGL_BUF(old_component_version_mem));
        GglBuffer old_component_version;
        ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"), ggl_kv_key(*pair), GGL_STR("version")
            ),
            &alloc,
            &old_component_version
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGD("Failed to get component version from config, "
                     "assuming component is new.");
        } else {
            if (ggl_buffer_eq(pair_val, old_component_version)) {
                GGL_LOGD(
                    "Detected that component %.*s has not changed version.",
                    (int) ggl_kv_key(*pair).len,
                    ggl_kv_key(*pair).data
                );
                component_updated = false;
            }
        }

        static uint8_t component_arn_buffer[256];
        alloc = ggl_arena_init(GGL_BUF(component_arn_buffer));
        GglBuffer component_arn;
        GglError arn_ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"), ggl_kv_key(*pair), GGL_STR("arn")
            ),
            &alloc,
            &component_arn
        );
        if (arn_ret != GGL_ERR_OK) {
            // TODO: Check over artifacts list even if local deployment and
            // attempt download if needed
            GGL_LOGW("Failed to retrieve arn. Assuming recipe artifacts "
                     "are found on-disk.");
        } else if (!component_updated) {
            // TODO: Check artifact hashes to see if artifacts have changed/need
            // to be redownloaded
            GGL_LOGD("Not retrieving component artifacts as the version has "
                     "not changed.");
        } else if (!tes_creds_retrieved) {
            if (deployment->type != LOCAL_DEPLOYMENT) {
                GGL_LOGE(
                    "TES credentials were not retrieved and deployment is not "
                    "a local deployment. Unable to do artifact retrieval."
                );
                return;
            }
            GGL_LOGW(
                "TES credentials were not retrieved, but deployment "
                "is local. Skipping artifact retrieval for component %.*s and "
                "attempting "
                "to complete deployment.",
                (int) ggl_kv_key(*pair).len,
                ggl_kv_key(*pair).data
            );
        } else {
            ret = get_recipe_artifacts(
                component_arn,
                tes_credentials,
                iot_credentials,
                ggl_obj_into_map(recipe_obj),
                component_artifacts_fd,
                component_archive_dir_fd,
                digest_context
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to get artifacts from recipe.");
                return;
            }
        }

        ret = ggl_gg_config_write(
            GGL_BUF_LIST(
                GGL_STR("services"), ggl_kv_key(*pair), GGL_STR("version")
            ),
            *ggl_kv_val(pair),
            &(int64_t) { 0 }
        );

        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to write version of %.*s to ggconfigd.",
                (int) ggl_kv_key(*pair).len,
                ggl_kv_key(*pair).data
            );
            return;
        }

        ret = add_arn_list_to_config(
            ggl_kv_key(*pair), deployment->configuration_arn
        );

        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to write configuration arn of %.*s to ggconfigd.",
                (int) ggl_kv_key(*pair).len,
                ggl_kv_key(*pair).data
            );
            return;
        }

        ret = apply_configurations(
            deployment, ggl_kv_key(*pair), GGL_STR("reset")
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to apply reset configuration update for %.*s.",
                (int) ggl_kv_key(*pair).len,
                ggl_kv_key(*pair).data
            );
            return;
        }

        GglObject *intermediate_obj;
        GglObject *default_config_obj;

        if (ggl_map_get(
                ggl_obj_into_map(recipe_obj),
                GGL_STR("ComponentConfiguration"),
                &intermediate_obj
            )) {
            if (ggl_obj_type(*intermediate_obj) != GGL_TYPE_MAP) {
                GGL_LOGE("ComponentConfiguration is not a map type");
                return;
            }

            if (ggl_map_get(
                    ggl_obj_into_map(*intermediate_obj),
                    GGL_STR("DefaultConfiguration"),
                    &default_config_obj
                )) {
                ret = ggl_gg_config_write(
                    GGL_BUF_LIST(
                        GGL_STR("services"),
                        ggl_kv_key(*pair),
                        GGL_STR("configuration")
                    ),
                    *default_config_obj,
                    &(int64_t) { 0 }
                );

                if (ret != GGL_ERR_OK) {
                    GGL_LOGE("Failed to send default config to ggconfigd.");
                    return;
                }
            } else {
                GGL_LOGI(
                    "DefaultConfiguration not found in the recipe of %.*s.",
                    (int) ggl_kv_key(*pair).len,
                    ggl_kv_key(*pair).data
                );
            }
        } else {
            GGL_LOGI(
                "ComponentConfiguration not found in the recipe of %.*s.",
                (int) ggl_kv_key(*pair).len,
                ggl_kv_key(*pair).data
            );
        }

        ret = apply_configurations(
            deployment, ggl_kv_key(*pair), GGL_STR("merge")
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to apply merge configuration update for %.*s.",
                (int) ggl_kv_key(*pair).len,
                ggl_kv_key(*pair).data
            );
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

        static Recipe2UnitArgs recipe2unit_args;
        memset(&recipe2unit_args, 0, sizeof(Recipe2UnitArgs));
        recipe2unit_args.user = posix_user;
        recipe2unit_args.group = group;

        recipe2unit_args.component_name = ggl_kv_key(*pair);
        recipe2unit_args.component_version = pair_val;

        memcpy(
            recipe2unit_args.recipe_runner_path,
            recipe_runner_path_vec.buf.data,
            recipe_runner_path_vec.buf.len
        );
        memcpy(
            recipe2unit_args.root_dir, args->root_path.data, args->root_path.len
        );
        recipe2unit_args.root_path_fd = root_path_fd;

        GglObject recipe_buff_obj;
        GglObject *component_name;
        static uint8_t unit_convert_alloc_mem[GGL_COMPONENT_RECIPE_MAX_LEN];
        GglArena unit_convert_alloc
            = ggl_arena_init(GGL_BUF(unit_convert_alloc_mem));
        HasPhase phases = { 0 };
        GglError err = convert_to_unit(
            &recipe2unit_args,
            &unit_convert_alloc,
            &recipe_buff_obj,
            &component_name,
            &phases
        );

        if (err != GGL_ERR_OK) {
            return;
        }

        if (!ggl_buffer_eq(
                ggl_obj_into_buf(*component_name), ggl_kv_key(*pair)
            )) {
            GGL_LOGE("Component name from recipe does not match component name "
                     "from recipe file.");
            return;
        }

        if (component_updated) {
            ret = ggl_kv_vec_push(
                &components_to_deploy,
                ggl_kv(ggl_kv_key(*pair), *ggl_kv_val(pair))
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "Failed to add component info for %.*s to deployment "
                    "vector.",
                    (int) ggl_kv_key(*pair).len,
                    ggl_kv_key(*pair).data
                );
                return;
            }
            GGL_LOGD(
                "Added %.*s to list of components that need to be processed.",
                (int) ggl_kv_key(*pair).len,
                ggl_kv_key(*pair).data
            );
        } else {
            // component already exists, check its lifecycle state
            GglArena component_status_alloc
                = ggl_arena_init(GGL_BUF((uint8_t[NAME_MAX]) { 0 }));
            GglBuffer component_status;
            ret = ggl_gghealthd_retrieve_component_status(
                ggl_kv_key(*pair), &component_status_alloc, &component_status
            );

            if (ret != GGL_ERR_OK) {
                GGL_LOGD(
                    "Failed to retrieve health status for %.*s. Redeploying "
                    "component.",
                    (int) ggl_kv_key(*pair).len,
                    ggl_kv_key(*pair).data
                );
                ret = ggl_kv_vec_push(
                    &components_to_deploy,
                    ggl_kv(ggl_kv_key(*pair), *ggl_kv_val(pair))
                );
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE(
                        "Failed to add component info for %.*s to deployment "
                        "vector.",
                        (int) ggl_kv_key(*pair).len,
                        ggl_kv_key(*pair).data
                    );
                    return;
                }
                GGL_LOGD(
                    "Added %.*s to list of components that need to be "
                    "processed.",
                    (int) ggl_kv_key(*pair).len,
                    ggl_kv_key(*pair).data
                );
            }

            // Skip redeploying components in a RUNNING state
            if (ggl_buffer_eq(component_status, GGL_STR("RUNNING"))
                || ggl_buffer_eq(component_status, GGL_STR("FINISHED"))) {
                GGL_LOGD(
                    "Component %.*s is already running. Will not redeploy.",
                    (int) ggl_kv_key(*pair).len,
                    ggl_kv_key(*pair).data
                );
                // save as a deployed component in case of bootstrap
                ret = save_component_info(
                    ggl_kv_key(*pair), pair_val, GGL_STR("completed")
                );
                if (ret != GGL_ERR_OK) {
                    return;
                }
            } else {
                ret = ggl_kv_vec_push(
                    &components_to_deploy,
                    ggl_kv(ggl_kv_key(*pair), *ggl_kv_val(pair))
                );
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE(
                        "Failed to add component info for %.*s to deployment "
                        "vector.",
                        (int) ggl_kv_key(*pair).len,
                        ggl_kv_key(*pair).data
                    );
                    return;
                }
                GGL_LOGD(
                    "Added %.*s to list of components that need to be "
                    "processed.",
                    (int) ggl_kv_key(*pair).len,
                    ggl_kv_key(*pair).data
                );
            }
        }
    }

    // TODO: Add a logic to only run the phases that exist with the latest
    // deployment
    if (components_to_deploy.map.len != 0) {
        // collect all component names that have relevant bootstrap service
        // files
        static GglBuffer bootstrap_comp_name_buf[MAX_COMP_NAME_BUF_SIZE];
        GglBufVec bootstrap_comp_name_buf_vec
            = GGL_BUF_VEC(bootstrap_comp_name_buf);

        ret = process_bootstrap_phase(
            components_to_deploy.map,
            args->root_path,
            &bootstrap_comp_name_buf_vec,
            deployment
        );
        if (ret != GGL_ERR_OK) {
            return;
        }

        // wait for all the bootstrap status
        ret = wait_for_phase_status(
            bootstrap_comp_name_buf_vec, GGL_STR("bootstrap")
        );
        if (ret != GGL_ERR_OK) {
            return;
        }

        // collect all component names that have relevant install service
        // files
        static GglBuffer install_comp_name_buf[MAX_COMP_NAME_BUF_SIZE];
        GglBufVec install_comp_name_buf_vec
            = GGL_BUF_VEC(install_comp_name_buf);

        // process all install files
        GGL_MAP_FOREACH (component, components_to_deploy.map) {
            GglBuffer component_name = ggl_kv_key(*component);

            static uint8_t install_service_file_path_buf[PATH_MAX];
            GglByteVec install_service_file_path_vec
                = GGL_BYTE_VEC(install_service_file_path_buf);
            ret = ggl_byte_vec_append(
                &install_service_file_path_vec, args->root_path
            );
            ggl_byte_vec_chain_append(
                &ret, &install_service_file_path_vec, GGL_STR("/")
            );
            ggl_byte_vec_chain_append(
                &ret, &install_service_file_path_vec, GGL_STR("ggl.")
            );
            ggl_byte_vec_chain_append(
                &ret, &install_service_file_path_vec, component_name
            );
            ggl_byte_vec_chain_append(
                &ret,
                &install_service_file_path_vec,
                GGL_STR(".install.service")
            );
            if (ret == GGL_ERR_OK) {
                // check if the current component name has relevant install
                // service file created
                int fd = -1;
                ret = ggl_file_open(
                    install_service_file_path_vec.buf, O_RDONLY, 0, &fd
                );
                if (ret != GGL_ERR_OK) {
                    GGL_LOGD(
                        "Component %.*s does not have the relevant install "
                        "service file",
                        (int) component_name.len,
                        component_name.data
                    );
                } else { // relevant install service file exists
                    (void) disable_and_unlink_service(&component_name, INSTALL);
                    // add relevant component name into the vector
                    ret = ggl_buf_vec_push(
                        &install_comp_name_buf_vec, component_name
                    );
                    if (ret != GGL_ERR_OK) {
                        GGL_LOGE("Failed to add the install component name "
                                 "into vector");
                        return;
                    }

                    // initiate link command for 'install'
                    static uint8_t link_command_buf[PATH_MAX];
                    GglByteVec link_command_vec
                        = GGL_BYTE_VEC(link_command_buf);
                    ret = ggl_byte_vec_append(
                        &link_command_vec, GGL_STR("systemctl link ")
                    );
                    ggl_byte_vec_chain_append(
                        &ret,
                        &link_command_vec,
                        install_service_file_path_vec.buf
                    );
                    ggl_byte_vec_chain_push(&ret, &link_command_vec, '\0');
                    if (ret != GGL_ERR_OK) {
                        GGL_LOGE(
                            "Failed to create systemctl link command for:%.*s",
                            (int) install_service_file_path_vec.buf.len,
                            install_service_file_path_vec.buf.data
                        );
                        return;
                    }

                    GGL_LOGD(
                        "Command to execute: %.*s",
                        (int) link_command_vec.buf.len,
                        link_command_vec.buf.data
                    );

                    // NOLINTBEGIN(concurrency-mt-unsafe)
                    int system_ret = system((char *) link_command_vec.buf.data);
                    if (WIFEXITED(system_ret)) {
                        if (WEXITSTATUS(system_ret) != 0) {
                            GGL_LOGE(
                                "systemctl link failed for:%.*s",
                                (int) install_service_file_path_vec.buf.len,
                                install_service_file_path_vec.buf.data
                            );
                            return;
                        }
                        GGL_LOGI(
                            "systemctl link exited for %.*s with child status "
                            "%d\n",
                            (int) install_service_file_path_vec.buf.len,
                            install_service_file_path_vec.buf.data,
                            WEXITSTATUS(system_ret)
                        );
                    } else {
                        GGL_LOGE(
                            "systemctl link did not exit normally for %.*s",
                            (int) install_service_file_path_vec.buf.len,
                            install_service_file_path_vec.buf.data
                        );
                        return;
                    }

                    // initiate start command for 'install'
                    static uint8_t start_command_buf[PATH_MAX];
                    GglByteVec start_command_vec
                        = GGL_BYTE_VEC(start_command_buf);
                    ret = ggl_byte_vec_append(
                        &start_command_vec, GGL_STR("systemctl start ")
                    );
                    ggl_byte_vec_chain_append(
                        &ret, &start_command_vec, GGL_STR("ggl.")
                    );
                    ggl_byte_vec_chain_append(
                        &ret, &start_command_vec, component_name
                    );
                    ggl_byte_vec_chain_append(
                        &ret, &start_command_vec, GGL_STR(".install.service\0")
                    );

                    GGL_LOGD(
                        "Command to execute: %.*s",
                        (int) start_command_vec.buf.len,
                        start_command_vec.buf.data
                    );
                    if (ret != GGL_ERR_OK) {
                        GGL_LOGE(
                            "Failed to create systemctl start command for %.*s",
                            (int) install_service_file_path_vec.buf.len,
                            install_service_file_path_vec.buf.data
                        );
                        return;
                    }

                    system_ret = system((char *) start_command_vec.buf.data);
                    // NOLINTEND(concurrency-mt-unsafe)
                    if (WIFEXITED(system_ret)) {
                        if (WEXITSTATUS(system_ret) != 0) {
                            GGL_LOGE(
                                "systemctl start failed for%.*s",
                                (int) install_service_file_path_vec.buf.len,
                                install_service_file_path_vec.buf.data
                            );
                            return;
                        }
                        GGL_LOGI(
                            "systemctl start exited with child status %d\n",
                            WEXITSTATUS(system_ret)
                        );
                    } else {
                        GGL_LOGE(
                            "systemctl start did not exit normally for %.*s",
                            (int) install_service_file_path_vec.buf.len,
                            install_service_file_path_vec.buf.data
                        );
                        return;
                    }
                }
            }
        }

        // wait for all the install status
        ret = wait_for_phase_status(
            install_comp_name_buf_vec, GGL_STR("install")
        );
        if (ret != GGL_ERR_OK) {
            return;
        }

        // process all run or startup files after install only
        GGL_MAP_FOREACH (component, components_to_deploy.map) {
            GglBuffer component_name = ggl_kv_key(*component);
            GglBuffer component_version
                = ggl_obj_into_buf(*ggl_kv_val(component));

            static uint8_t service_file_path_buf[PATH_MAX];
            GglByteVec service_file_path_vec
                = GGL_BYTE_VEC(service_file_path_buf);
            ret = ggl_byte_vec_append(&service_file_path_vec, args->root_path);
            ggl_byte_vec_chain_append(
                &ret, &service_file_path_vec, GGL_STR("/")
            );
            ggl_byte_vec_chain_append(
                &ret, &service_file_path_vec, GGL_STR("ggl.")
            );
            ggl_byte_vec_chain_append(
                &ret, &service_file_path_vec, component_name
            );
            ggl_byte_vec_chain_append(
                &ret, &service_file_path_vec, GGL_STR(".service")
            );
            if (ret == GGL_ERR_OK) {
                // check if the current component name has relevant run
                // service file created
                int fd = -1;
                ret = ggl_file_open(
                    service_file_path_vec.buf, O_RDONLY, 0, &fd
                );
                if (ret != GGL_ERR_OK) {
                    GGL_LOGD(
                        "Component %.*s does not have the relevant run "
                        "service file",
                        (int) component_name.len,
                        component_name.data
                    );
                } else {
                    (void
                    ) disable_and_unlink_service(&component_name, RUN_STARTUP);
                    // run link command
                    static uint8_t link_command_buf[PATH_MAX];
                    GglByteVec link_command_vec
                        = GGL_BYTE_VEC(link_command_buf);
                    ret = ggl_byte_vec_append(
                        &link_command_vec, GGL_STR("systemctl link ")
                    );
                    ggl_byte_vec_chain_append(
                        &ret, &link_command_vec, service_file_path_vec.buf
                    );
                    ggl_byte_vec_chain_push(&ret, &link_command_vec, '\0');
                    if (ret != GGL_ERR_OK) {
                        GGL_LOGE("Failed to create systemctl link command.");
                        return;
                    }

                    GGL_LOGD(
                        "Command to execute: %.*s",
                        (int) link_command_vec.buf.len,
                        link_command_vec.buf.data
                    );

                    // NOLINTNEXTLINE(concurrency-mt-unsafe)
                    int system_ret = system((char *) link_command_vec.buf.data);
                    if (WIFEXITED(system_ret)) {
                        if (WEXITSTATUS(system_ret) != 0) {
                            GGL_LOGE("systemctl link command failed");
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

                    // run enable command
                    static uint8_t enable_command_buf[PATH_MAX];
                    GglByteVec enable_command_vec
                        = GGL_BYTE_VEC(enable_command_buf);
                    ret = ggl_byte_vec_append(
                        &enable_command_vec, GGL_STR("systemctl enable ")
                    );
                    ggl_byte_vec_chain_append(
                        &ret, &enable_command_vec, service_file_path_vec.buf
                    );
                    ggl_byte_vec_chain_push(&ret, &enable_command_vec, '\0');
                    if (ret != GGL_ERR_OK) {
                        GGL_LOGE("Failed to create systemctl enable command.");
                        return;
                    }
                    GGL_LOGD(
                        "Command to execute: %.*s",
                        (int) enable_command_vec.buf.len,
                        enable_command_vec.buf.data
                    );

                    // NOLINTNEXTLINE(concurrency-mt-unsafe)
                    system_ret = system((char *) enable_command_vec.buf.data);
                    if (WIFEXITED(system_ret)) {
                        if (WEXITSTATUS(system_ret) != 0) {
                            GGL_LOGE("systemctl enable failed");
                            return;
                        }
                        GGL_LOGI(
                            "systemctl enable exited with child status "
                            "%d\n",
                            WEXITSTATUS(system_ret)
                        );
                    } else {
                        GGL_LOGE("systemctl enable did not exit normally");
                        return;
                    }
                }
            }

            // save as a deployed component in case of bootstrap
            ret = save_component_info(
                component_name, component_version, GGL_STR("completed")
            );
            if (ret != GGL_ERR_OK) {
                return;
            }
        }

        // run daemon-reload command once all the files are linked
        static uint8_t reload_command_buf[PATH_MAX];
        GglByteVec reload_command_vec = GGL_BYTE_VEC(reload_command_buf);
        ret = ggl_byte_vec_append(
            &reload_command_vec, GGL_STR("systemctl daemon-reload\0")
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to create systemctl daemon-reload command.");
            return;
        }
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        int system_ret = system((char *) reload_command_vec.buf.data);
        if (WIFEXITED(system_ret)) {
            if (WEXITSTATUS(system_ret) != 0) {
                GGL_LOGE("systemctl daemon-reload failed");
                return;
            }
            GGL_LOGI(
                "systemctl daemon-reload exited with child status %d\n",
                WEXITSTATUS(system_ret)
            );
        } else {
            GGL_LOGE("systemctl daemon-reload did not exit normally");
            return;
        }
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    int system_ret = system("systemctl reset-failed");
    (void) (system_ret);
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    system_ret = system("systemctl start greengrass-lite.target");
    (void) (system_ret);

    ret = wait_for_deployment_status(resolved_components_kv_vec.map);
    if (ret != GGL_ERR_OK) {
        return;
    }

    GGL_LOGI("Performing cleanup of stale components");
    ret = cleanup_stale_versions(resolved_components_kv_vec.map);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error while cleaning up stale components after deployment.");
    }

    *deployment_succeeded = true;
}

static GglError ggl_deployment_listen(GglDeploymentHandlerThreadArgs *args) {
    // check for in progress deployment in case of bootstrap
    GglDeployment bootstrap_deployment = { 0 };
    uint8_t jobs_id_resp_mem[64] = { 0 };
    GglBuffer jobs_id = GGL_BUF(jobs_id_resp_mem);
    int64_t jobs_version = 0;

    GglError ret = retrieve_in_progress_deployment(
        &bootstrap_deployment, &jobs_id, &jobs_version
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGD("No deployments previously in progress detected.");
    } else {
        GGL_LOGI(
            "Found previously in progress deployment %.*s. Resuming "
            "deployment.",
            (int) bootstrap_deployment.deployment_id.len,
            bootstrap_deployment.deployment_id.data
        );

        bool send_deployment_update
            = (GGL_ERR_OK
               == set_jobs_deployment_for_bootstrap(
                   jobs_id, bootstrap_deployment.deployment_id, jobs_version
               ));

        bool bootstrap_deployment_succeeded = false;
        handle_deployment(
            &bootstrap_deployment, args, &bootstrap_deployment_succeeded
        );

        (void) send_fss_update(
            &bootstrap_deployment, bootstrap_deployment_succeeded
        );

        if (send_deployment_update && bootstrap_deployment_succeeded) {
            GGL_LOGI("Completed deployment processing and reporting job as "
                     "SUCCEEDED.");
            (void) update_current_jobs_deployment(
                bootstrap_deployment.deployment_id, GGL_STR("SUCCEEDED")
            );
        } else if (send_deployment_update) {
            GGL_LOGW("Completed deployment processing and reporting job as "
                     "FAILED.");
            (void) update_current_jobs_deployment(
                bootstrap_deployment.deployment_id, GGL_STR("FAILED")
            );
        } else {
            GGL_LOGI("Completed deployment, but job was canceled.");
        }
        // clear any potential saved deployment info for next deployment
        ret = delete_saved_deployment_from_config();
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to delete saved deployment info from config.");
        }

        // TODO: investigate deployment queue behavior with bootstrap deployment
        ggl_deployment_release(&bootstrap_deployment);
    }

    while (true) {
        GglDeployment *deployment;
        // Since this is blocking, error is fatal
        ret = ggl_deployment_dequeue(&deployment);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        GGL_LOGI("Processing incoming deployment.");

        (void) update_current_jobs_deployment(
            deployment->deployment_id, GGL_STR("IN_PROGRESS")
        );

        bool deployment_succeeded = false;
        handle_deployment(deployment, args, &deployment_succeeded);

        (void) send_fss_update(deployment, deployment_succeeded);

        // TODO: need error details from handle_deployment
        if (deployment_succeeded) {
            GGL_LOGI("Completed deployment processing and reporting job as "
                     "SUCCEEDED.");
            (void) update_current_jobs_deployment(
                deployment->deployment_id, GGL_STR("SUCCEEDED")
            );
        } else {
            GGL_LOGW("Completed deployment processing and reporting job as "
                     "FAILED.");
            (void) update_current_jobs_deployment(
                deployment->deployment_id, GGL_STR("FAILED")
            );
        }
        // clear any potential saved deployment info for next deployment
        ret = delete_saved_deployment_from_config();
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to delete saved deployment info from config.");
        }

        ggl_deployment_release(deployment);
    }
}

void *ggl_deployment_handler_thread(void *ctx) {
    GGL_LOGD("Starting deployment processing thread.");

    (void) ggl_deployment_listen(ctx);

    GGL_LOGE("Deployment thread exiting due to failure.");

    // clear any potential saved deployment info for next deployment
    GglError ret = delete_saved_deployment_from_config();
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to delete saved deployment info from config.");
    }

    // This is safe as long as only this thread will ever call exit.

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);

    return NULL;
}
