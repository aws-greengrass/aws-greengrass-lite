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
#include <gg/arena.h>
#include <gg/backoff.h>
#include <gg/base64.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/flags.h>
#include <gg/json_decode.h>
#include <gg/json_encode.h>
#include <gg/list.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/utils.h>
#include <gg/vector.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/core_bus/gg_healthd.h>
#include <ggl/core_bus/sub_response.h>
#include <ggl/digest.h>
#include <ggl/docker_client.h>
#include <ggl/http.h>
#include <ggl/nucleus/constants.h>
#include <ggl/process.h>
#include <ggl/recipe.h>
#include <ggl/recipe2unit.h>
#include <ggl/semver.h>
#include <ggl/uri.h>
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
    GgBuffer aws_region;
    GgBuffer access_key_id;
    GgBuffer secret_access_key;
    GgBuffer session_token;
} TesCredentials;

// vector to track successfully deployed components to be saved for bootstrap
// component name -> map of lifecycle state and version
// static GgKVVec deployed_components = GG_KV_VEC((GgKV[64]) { 0 });

static SigV4Details sigv4_from_tes(
    TesCredentials credentials, GgBuffer aws_service
) {
    return (SigV4Details) { .aws_region = credentials.aws_region,
                            .aws_service = aws_service,
                            .access_key_id = credentials.access_key_id,
                            .secret_access_key = credentials.secret_access_key,
                            .session_token = credentials.session_token };
}

static GgError merge_dir_to(GgBuffer source, const char *dir) {
    const char *mkdir[] = { "mkdir", "-p", dir, NULL };
    GgError ret = ggl_process_call(mkdir);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // Append /. so that contents get copied, not dir
    static char source_path[PATH_MAX];
    GgByteVec source_path_vec = GG_BYTE_VEC(source_path);
    ret = gg_byte_vec_append(&source_path_vec, source);
    gg_byte_vec_chain_append(&ret, &source_path_vec, GG_STR("/.\0"));
    if (ret != GG_ERR_OK) {
        return ret;
    }

    const char *cp[] = { "cp", "-RP", source_path, dir, NULL };
    return ggl_process_call(cp);
}

static GgError get_thing_name(char **thing_name) {
    static uint8_t resp_mem[129] = { 0 };
    GgArena alloc = gg_arena_init(
        gg_buffer_substr(GG_BUF(resp_mem), 0, sizeof(resp_mem) - 1)
    );
    GgBuffer resp = { 0 };

    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("system"), GG_STR("thingName")), &alloc, &resp
    );
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to get thing name from config.");
        return ret;
    }

    resp_mem[resp.len] = '\0';

    *thing_name = (char *) resp_mem;
    return GG_ERR_OK;
}

static GgError get_region(GgByteVec *region) {
    static uint8_t resp_mem[128] = { 0 };
    GgArena alloc = gg_arena_init(GG_BUF(resp_mem));
    GgBuffer resp;

    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("awsRegion")
        ),
        &alloc,
        &resp
    );
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to get region from config.");
        return ret;
    }

    gg_byte_vec_chain_append(&ret, region, resp);
    gg_byte_vec_chain_push(&ret, region, '\0');
    if (ret == GG_ERR_OK) {
        region->buf.len--;
    }
    return ret;
}

static GgError get_posix_user(char **posix_user) {
    static uint8_t resp_mem[129] = { 0 };
    GgArena alloc = gg_arena_init(
        gg_buffer_substr(GG_BUF(resp_mem), 0, sizeof(resp_mem) - 1)
    );
    GgBuffer resp = GG_BUF(resp_mem);

    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("runWithDefault"),
            GG_STR("posixUser")
        ),
        &alloc,
        &resp
    );
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to get posixUser from config.");
        return ret;
    }

    if (resp.len == 0) {
        GG_LOGW("posixUser is empty.");
        return GG_ERR_INVALID;
    }

    resp_mem[resp.len] = '\0';

    *posix_user = (char *) resp_mem;
    return GG_ERR_OK;
}

static GgError get_data_endpoint(GgByteVec *endpoint) {
    GgMap params = GG_MAP(gg_kv(
        GG_STR("key_path"),
        gg_obj_list(GG_LIST(
            gg_obj_buf(GG_STR("services")),
            gg_obj_buf(GG_STR("aws.greengrass.NucleusLite")),
            gg_obj_buf(GG_STR("configuration")),
            gg_obj_buf(GG_STR("iotDataEndpoint"))
        ))
    ));

    static uint8_t resp_mem[128] = { 0 };
    GgArena alloc = gg_arena_init(
        gg_buffer_substr(GG_BUF(resp_mem), 0, sizeof(resp_mem) - 1)
    );

    GgObject resp;
    GgError ret = ggl_call(
        GG_STR("gg_config"), GG_STR("read"), params, NULL, &alloc, &resp
    );
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to get dataplane endpoint from config.");
        return ret;
    }
    if (gg_obj_type(resp) != GG_TYPE_BUF) {
        GG_LOGE("Configuration dataplane endpoint is not a string.");
        return GG_ERR_INVALID;
    }

    return gg_byte_vec_append(endpoint, gg_obj_into_buf(resp));
}

static GgError get_data_port(GgByteVec *port) {
    GgMap params = GG_MAP(gg_kv(
        GG_STR("key_path"),
        gg_obj_list(GG_LIST(
            gg_obj_buf(GG_STR("services")),
            gg_obj_buf(GG_STR("aws.greengrass.NucleusLite")),
            gg_obj_buf(GG_STR("configuration")),
            gg_obj_buf(GG_STR("greengrassDataPlanePort"))
        ))
    ));

    static uint8_t resp_mem[128] = { 0 };
    GgArena alloc = gg_arena_init(
        gg_buffer_substr(GG_BUF(resp_mem), 0, sizeof(resp_mem) - 1)
    );

    GgObject resp;
    GgError ret = ggl_call(
        GG_STR("gg_config"), GG_STR("read"), params, NULL, &alloc, &resp
    );
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to get dataplane port from config.");
        return ret;
    }
    if (gg_obj_type(resp) != GG_TYPE_BUF) {
        GG_LOGE("Configuration dataplane port is not a string.");
        return GG_ERR_INVALID;
    }

    return gg_byte_vec_append(port, gg_obj_into_buf(resp));
}

static GgError get_private_key_path(GgByteVec *pkey_path) {
    GgMap params = GG_MAP(gg_kv(
        GG_STR("key_path"),
        gg_obj_list(GG_LIST(
            gg_obj_buf(GG_STR("system")), gg_obj_buf(GG_STR("privateKeyPath"))
        ))
    ));

    uint8_t resp_mem[128] = { 0 };
    GgArena alloc = gg_arena_init(
        gg_buffer_substr(GG_BUF(resp_mem), 0, sizeof(resp_mem) - 1)
    );

    GgObject resp;
    GgError ret = ggl_call(
        GG_STR("gg_config"), GG_STR("read"), params, NULL, &alloc, &resp
    );
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to get private key path from config.");
        return ret;
    }
    if (gg_obj_type(resp) != GG_TYPE_BUF) {
        GG_LOGE("Configuration private key path is not a string.");
        return GG_ERR_INVALID;
    }

    gg_byte_vec_chain_append(&ret, pkey_path, gg_obj_into_buf(resp));
    gg_byte_vec_chain_push(&ret, pkey_path, '\0');
    return ret;
}

static GgError get_cert_path(GgByteVec *cert_path) {
    GgMap params = GG_MAP(gg_kv(
        GG_STR("key_path"),
        gg_obj_list(GG_LIST(
            gg_obj_buf(GG_STR("system")),
            gg_obj_buf(GG_STR("certificateFilePath"))
        ))
    ));

    static uint8_t resp_mem[128] = { 0 };
    GgArena alloc = gg_arena_init(
        gg_buffer_substr(GG_BUF(resp_mem), 0, sizeof(resp_mem) - 1)
    );

    GgObject resp;
    GgError ret = ggl_call(
        GG_STR("gg_config"), GG_STR("read"), params, NULL, &alloc, &resp
    );
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to get certificate path from config.");
        return ret;
    }
    if (gg_obj_type(resp) != GG_TYPE_BUF) {
        GG_LOGE("Configuration certificate path is not a string.");
        return GG_ERR_INVALID;
    }

    gg_byte_vec_chain_append(&ret, cert_path, gg_obj_into_buf(resp));
    gg_byte_vec_chain_push(&ret, cert_path, '\0');
    return ret;
}

static GgError get_rootca_path(GgByteVec *rootca_path) {
    GgMap params = GG_MAP(gg_kv(
        GG_STR("key_path"),
        gg_obj_list(GG_LIST(
            gg_obj_buf(GG_STR("system")), gg_obj_buf(GG_STR("rootCaPath"))
        ))
    ));

    static uint8_t resp_mem[128] = { 0 };
    GgArena alloc = gg_arena_init(
        gg_buffer_substr(GG_BUF(resp_mem), 0, sizeof(resp_mem) - 1)
    );

    GgObject resp;
    GgError ret = ggl_call(
        GG_STR("gg_config"), GG_STR("read"), params, NULL, &alloc, &resp
    );
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to get rootca path from config.");
        return ret;
    }
    if (gg_obj_type(resp) != GG_TYPE_BUF) {
        GG_LOGE("Configuration rootca path is not a string.");
        return GG_ERR_INVALID;
    }

    gg_byte_vec_chain_append(&ret, rootca_path, gg_obj_into_buf(resp));
    gg_byte_vec_chain_push(&ret, rootca_path, '\0');
    return ret;
}

static GgError get_tes_credentials(TesCredentials *tes_creds) {
    GgObject *aws_access_key_id = NULL;
    GgObject *aws_secret_access_key = NULL;
    GgObject *aws_session_token = NULL;

    static uint8_t credentials_alloc[1500];
    static GgBuffer tesd = GG_STR("aws_iot_tes");
    GgObject result;
    GgMap params = { 0 };
    GgArena credential_alloc = gg_arena_init(GG_BUF(credentials_alloc));

    GgError ret = ggl_call(
        tesd,
        GG_STR("request_credentials"),
        params,
        NULL,
        &credential_alloc,
        &result
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get TES credentials.");
        return GG_ERR_FAILURE;
    }

    ret = gg_map_validate(
        gg_obj_into_map(result),
        GG_MAP_SCHEMA(
            { GG_STR("accessKeyId"),
              GG_REQUIRED,
              GG_TYPE_BUF,
              &aws_access_key_id },
            { GG_STR("secretAccessKey"),
              GG_REQUIRED,
              GG_TYPE_BUF,
              &aws_secret_access_key },
            { GG_STR("sessionToken"),
              GG_REQUIRED,
              GG_TYPE_BUF,
              &aws_session_token },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to validate TES credentials."

        );
        return GG_ERR_FAILURE;
    }
    tes_creds->access_key_id = gg_obj_into_buf(*aws_access_key_id);
    tes_creds->secret_access_key = gg_obj_into_buf(*aws_secret_access_key);
    tes_creds->session_token = gg_obj_into_buf(*aws_session_token);
    return GG_ERR_OK;
}

typedef struct {
    const char *url_for_sigv4_download;
    GgBuffer host;
    GgBuffer file_path;
    SigV4Details sigv4_details;

    // reset response_data for next attempt
    GgError (*retry_cleanup_fn)(void *);
    void *response_data;

    // Needed to propagate errors when retrying is impossible.
    GgError err;
} DownloadRequestRetryCtx;

static GgError retry_download_wrapper(void *ctx) {
    DownloadRequestRetryCtx *retry_ctx = (DownloadRequestRetryCtx *) ctx;
    uint16_t http_response_code;

    GgError ret = sigv4_download(
        retry_ctx->url_for_sigv4_download,
        retry_ctx->host,
        retry_ctx->file_path,
        *(int *) retry_ctx->response_data,
        retry_ctx->sigv4_details,
        &http_response_code
    );
    if (http_response_code == (uint16_t) 403) {
        GgError err = retry_ctx->retry_cleanup_fn(retry_ctx->response_data);
        GG_LOGE(
            "Artifact download attempt failed with 403. Retrying with backoff."
        );
        if (err != GG_ERR_OK) {
            retry_ctx->err = err;
            return GG_ERR_OK;
        }
        return GG_ERR_FAILURE;
    }
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Artifact download attempt failed due to error: %d", ret

        );
        retry_ctx->err = ret;
        return GG_ERR_OK;
    }

    retry_ctx->err = ret;
    return GG_ERR_OK;
}

// TODO: Refactor to delete the file and get the new fd instead of truncating
// the file
static GgError truncate_s3_file_on_failure(void *response_data) {
    int fd = *(int *) response_data;

    int ret;
    do {
        ret = ftruncate(fd, 0);
    } while ((ret == -1) && (errno == EINTR));

    if (ret == -1) {
        GG_LOGE("Failed to truncate fd for write (errno=%d).", errno);
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

static GgError retryable_download_request(
    const char *url_for_sigv4_download,
    GgBuffer host,
    GgBuffer file_path,
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
            .err = GG_ERR_OK };

    GgError ret
        = gg_backoff(3000, 64000, 3, retry_download_wrapper, (void *) &ctx);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Artifact download attempt failed; retries exhausted.");
        return ret;
    }
    if (ctx.err != GG_ERR_OK) {
        return ctx.err;
    }
    return GG_ERR_OK;
}

static GgError download_s3_artifact(
    GgBuffer scratch_buffer,
    GglUriInfo uri_info,
    TesCredentials credentials,
    int artifact_fd
) {
    GgByteVec url_vec = gg_byte_vec_init(scratch_buffer);
    GgError error = GG_ERR_OK;
    size_t start_loc = 0;
    size_t end_loc = 0;
    size_t file_name_end = 0;
    gg_byte_vec_chain_append(&error, &url_vec, GG_STR("https://"));
    start_loc = url_vec.buf.len;
    gg_byte_vec_chain_append(&error, &url_vec, uri_info.host);
    gg_byte_vec_chain_append(&error, &url_vec, GG_STR(".s3."));
    gg_byte_vec_chain_append(&error, &url_vec, credentials.aws_region);
    gg_byte_vec_chain_append(&error, &url_vec, GG_STR(".amazonaws.com/"));
    end_loc = url_vec.buf.len - 1;
    gg_byte_vec_chain_append(&error, &url_vec, uri_info.path);
    file_name_end = url_vec.buf.len;
    gg_byte_vec_chain_push(&error, &url_vec, '\0');
    if (error != GG_ERR_OK) {
        return error;
    }

    return retryable_download_request(
        (const char *) url_vec.buf.data,
        (GgBuffer) { .data = &scratch_buffer.data[start_loc],
                     .len = end_loc - start_loc },
        (GgBuffer) { .data = &scratch_buffer.data[end_loc],
                     .len = file_name_end - end_loc },
        artifact_fd,
        sigv4_from_tes(credentials, GG_STR("s3"))
    );
}

static GgError download_greengrass_artifact(
    GgBuffer scratch_buffer,
    GgBuffer component_arn,
    GgBuffer uri_path,
    CertificateDetails credentials,
    int artifact_fd
) {
    // For holding a presigned S3 URL
    static uint8_t response_data[2000];

    GgError err = GG_ERR_OK;
    // https://docs.aws.amazon.com/greengrass/v2/APIReference/API_GetComponentVersionArtifact.html
    GgByteVec uri_path_vec = gg_byte_vec_init(scratch_buffer);
    gg_byte_vec_chain_append(
        &err, &uri_path_vec, GG_STR("greengrass/v2/components/")
    );
    gg_byte_vec_chain_append(&err, &uri_path_vec, component_arn);
    gg_byte_vec_chain_append(&err, &uri_path_vec, GG_STR("/artifacts/"));
    gg_byte_vec_chain_append(&err, &uri_path_vec, uri_path);
    if (err != GG_ERR_OK) {
        return err;
    }

    GG_LOGI("Getting presigned S3 URL");
    GgBuffer response_buffer = GG_BUF(response_data);
    err = gg_dataplane_call(
        gg_buffer_from_null_term(config.data_endpoint),
        gg_buffer_from_null_term(config.port),
        uri_path_vec.buf,
        credentials,
        NULL,
        &response_buffer
    );

    if (err != GG_ERR_OK) {
        return err;
    }

    // reusing scratch buffer for JSON decoding
    GgArena json_bump = gg_arena_init(scratch_buffer);
    GgObject response_obj;
    err = gg_json_decode_destructive(
        response_buffer, &json_bump, &response_obj
    );
    if (err != GG_ERR_OK) {
        return err;
    }
    if (gg_obj_type(response_obj) != GG_TYPE_MAP) {
        return GG_ERR_PARSE;
    }
    GgObject *presigned_url_obj;
    err = gg_map_validate(
        gg_obj_into_map(response_obj),
        GG_MAP_SCHEMA({ GG_STR("preSignedUrl"),
                        GG_REQUIRED,
                        GG_TYPE_BUF,
                        &presigned_url_obj })
    );
    if (err != GG_ERR_OK) {
        return GG_ERR_FAILURE;
    }
    GgBuffer presigned_url = gg_obj_into_buf(*presigned_url_obj);

    // Should be OK to null-terminate this buffer;
    // it's in the middle of a JSON blob.
    presigned_url.data[presigned_url.len] = '\0';

    GG_LOGI("Getting presigned S3 URL artifact");

    return generic_download((const char *) (presigned_url.data), artifact_fd);
}

// Get the unarchive type: NONE or ZIP
static GgError get_artifact_unarchive_type(
    GgBuffer unarchive_buf, bool *needs_unarchive
) {
    if (gg_buffer_eq(unarchive_buf, GG_STR("NONE"))) {
        *needs_unarchive = false;
    } else if (gg_buffer_eq(unarchive_buf, GG_STR("ZIP"))) {
        *needs_unarchive = true;
    } else {
        GG_LOGE("Unknown archive type");
        return GG_ERR_UNSUPPORTED;
    }
    return GG_ERR_OK;
}

static GgError unarchive_artifact(
    int component_store_fd,
    GgBuffer zip_file,
    mode_t mode,
    int component_archive_store_fd
) {
    GgBuffer destination_dir = zip_file;
    if (gg_buffer_has_suffix(zip_file, GG_STR(".zip"))) {
        destination_dir = gg_buffer_substr(
            zip_file, 0, zip_file.len - (sizeof(".zip") - 1U)
        );
    }

    GG_LOGD("Unarchive %.*s", (int) zip_file.len, zip_file.data);

    int output_dir_fd;
    GgError err = gg_dir_openat(
        component_archive_store_fd,
        destination_dir,
        O_PATH,
        true,
        &output_dir_fd
    );
    if (err != GG_ERR_OK) {
        GG_LOGE("Failed to open unarchived artifact location.");
        return err;
    }
    GG_CLEANUP(cleanup_close, output_dir_fd);

    // Unarchive the zip
    return ggl_zip_unarchive(component_store_fd, zip_file, output_dir_fd, mode);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GgError get_recipe_artifacts(
    GgBuffer component_arn,
    TesCredentials tes_creds,
    CertificateDetails iot_creds,
    GgMap recipe,
    int component_store_fd,
    int component_archive_store_fd,
    GglDigest digest_context
) {
    GgList artifacts = { 0 };
    GgError error = ggl_get_recipe_artifacts_for_platform(recipe, &artifacts);
    if (error != GG_ERR_OK) {
        return error;
    }

    bool ecr_logged_in = false;
    for (size_t i = 0; i < artifacts.len; ++i) {
        uint8_t decode_buffer[MAX_DECODE_BUF_LEN];
        if (gg_obj_type(artifacts.items[i]) != GG_TYPE_MAP) {
            return GG_ERR_PARSE;
        }
        GgObject *uri_obj = NULL;
        GgObject *unarchive_obj = NULL;
        GgObject *expected_digest_obj = NULL;
        GgObject *algorithm = NULL;

        GgError err = gg_map_validate(
            gg_obj_into_map(artifacts.items[i]),
            GG_MAP_SCHEMA(
                { GG_STR("Uri"), GG_REQUIRED, GG_TYPE_BUF, &uri_obj },
                { GG_STR("Unarchive"),
                  GG_OPTIONAL,
                  GG_TYPE_BUF,
                  &unarchive_obj },
                { GG_STR("Digest"),
                  GG_OPTIONAL,
                  GG_TYPE_BUF,
                  &expected_digest_obj },
                { GG_STR("Algorithm"), GG_OPTIONAL, GG_TYPE_BUF, &algorithm }
            )
        );

        if (err != GG_ERR_OK) {
            GG_LOGE("Failed to validate recipe artifact");
            return GG_ERR_PARSE;
        }

        GglUriInfo info = { 0 };
        {
            GgArena alloc = gg_arena_init(GG_BUF(decode_buffer));
            err = gg_uri_parse(&alloc, gg_obj_into_buf(*uri_obj), &info);
            if (err != GG_ERR_OK) {
                return err;
            }
        }

        if (gg_buffer_eq(GG_STR("docker"), info.scheme)) {
            GgBuffer docker_uri = info.path;
            GglDockerUriInfo docker_info = { 0 };
            err = gg_docker_uri_parse(docker_uri, &docker_info);
            if (err != GG_ERR_OK) {
                GG_LOGE(
                    "Failed to parse docker URI \"%.*s\"",
                    (int) docker_uri.len,
                    docker_uri.data
                );
                return err;
            }

            if (((docker_info.tag.len == 0) && (docker_info.digest.len == 0))
                || gg_buffer_eq(docker_info.tag, GG_STR("latest"))) {
                GG_LOGD("Latest tag requested. Pulling image.");
            } else if (ggl_docker_check_image(docker_uri) != GG_ERR_OK) {
                GG_LOGD("Image not found. Pulling image.");
            } else {
                GG_LOGD("Image already found, skipping.");
                continue;
            }

            if (!ecr_logged_in) {
                if (ggl_docker_is_uri_private_ecr(docker_info)) {
                    err = ggl_docker_credentials_ecr_retrieve(
                        docker_info, sigv4_from_tes(tes_creds, GG_STR("ecr"))
                    );
                    if (err != GG_ERR_OK) {
                        return GG_ERR_FAILURE;
                    }
                    ecr_logged_in = true;
                }
            }

            err = ggl_docker_pull(docker_uri);
            if (err != GG_ERR_OK) {
                return GG_ERR_FAILURE;
            }
            // Docker performs all other necessary checks.
            continue;
        }

        bool needs_verification = false;
        GgBuffer expected_digest;
        if (expected_digest_obj != NULL) {
            expected_digest = gg_obj_into_buf(*expected_digest_obj);

            if (algorithm != NULL) {
                if (!gg_buffer_eq(
                        gg_obj_into_buf(*algorithm), GG_STR("SHA-256")
                    )) {
                    GG_LOGE("Unsupported digest algorithm");
                    return GG_ERR_UNSUPPORTED;
                }
            } else {
                GG_LOGW("Assuming SHA-256 digest.");
            }

            if (!gg_base64_decode_in_place(&expected_digest)) {
                GG_LOGE("Failed to decode digest.");
                return GG_ERR_PARSE;
            }
            needs_verification = true;
        }

        bool needs_unarchive = false;
        if (unarchive_obj != NULL) {
            err = get_artifact_unarchive_type(
                gg_obj_into_buf(*unarchive_obj), &needs_unarchive
            );
            if (err != GG_ERR_OK) {
                return err;
            }
        }

        // TODO: set permissions from recipe
        mode_t mode = 0755;
        int artifact_fd = -1;
        err = gg_file_openat(
            component_store_fd,
            info.file,
            O_CREAT | O_WRONLY | O_TRUNC,
            needs_unarchive ? 0644 : mode,
            &artifact_fd
        );
        if (err != GG_ERR_OK) {
            GG_LOGE("Failed to create artifact file for write.");
            return err;
        }
        GG_CLEANUP(cleanup_close, artifact_fd);

        if (gg_buffer_eq(GG_STR("s3"), info.scheme)) {
            err = download_s3_artifact(
                GG_BUF(decode_buffer), info, tes_creds, artifact_fd
            );
        } else if (gg_buffer_eq(GG_STR("greengrass"), info.scheme)) {
            err = download_greengrass_artifact(
                GG_BUF(decode_buffer),
                component_arn,
                info.path,
                iot_creds,
                artifact_fd
            );
        } else {
            GG_LOGE("Unknown artifact URI scheme");
            err = GG_ERR_PARSE;
        }

        if (err != GG_ERR_OK) {
            return err;
        }

        err = gg_fsync(artifact_fd);
        if (err != GG_ERR_OK) {
            GG_LOGE("Artifact fsync failed.");
            return err;
        }

        // verify SHA256 digest
        if (needs_verification) {
            GG_LOGD("Verifying artifact digest");
            err = ggl_verify_sha256_digest(
                component_store_fd, info.file, expected_digest, digest_context
            );
            if (err != GG_ERR_OK) {
                return err;
            }
        }

        // Unarchive the ZIP file if needed
        if (needs_unarchive) {
            err = unarchive_artifact(
                component_store_fd, info.file, mode, component_archive_store_fd
            );
            if (err != GG_ERR_OK) {
                return err;
            }
        }
    }
    return GG_ERR_OK;
}

static GgError get_device_thing_groups(GgBuffer *response) {
    GgByteVec data_endpoint = GG_BYTE_VEC(config.data_endpoint);
    GgError ret = get_data_endpoint(&data_endpoint);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get dataplane endpoint.");
        return ret;
    }

    GgByteVec region = GG_BYTE_VEC(config.region);
    ret = get_region(&region);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get region.");
        return ret;
    }

    GgByteVec port = GG_BYTE_VEC(config.port);
    ret = get_data_port(&port);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get dataplane port.");
        return ret;
    }

    GgByteVec pkey_path = GG_BYTE_VEC(config.pkey_path);
    ret = get_private_key_path(&pkey_path);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get private key path.");
        return ret;
    }

    GgByteVec cert_path = GG_BYTE_VEC(config.cert_path);
    ret = get_cert_path(&cert_path);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get certificate path.");
        return ret;
    }

    GgByteVec rootca_path = GG_BYTE_VEC(config.rootca_path);
    ret = get_rootca_path(&rootca_path);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get certificate path.");
        return ret;
    }

    CertificateDetails cert_details
        = { .gghttplib_cert_path = config.cert_path,
            .gghttplib_root_ca_path = config.rootca_path,
            .gghttplib_p_key_path = config.pkey_path };

    char *thing_name = NULL;
    ret = get_thing_name(&thing_name);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get thing name.");
        return ret;
    }

    static uint8_t uri_path_buf[PATH_MAX];
    GgByteVec uri_path_vec = GG_BYTE_VEC(uri_path_buf);
    ret = gg_byte_vec_append(
        &uri_path_vec, GG_STR("greengrass/v2/coreDevices/")
    );
    gg_byte_vec_chain_append(
        &ret, &uri_path_vec, gg_buffer_from_null_term(thing_name)
    );
    gg_byte_vec_chain_append(&ret, &uri_path_vec, GG_STR("/thingGroups"));
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to create thing groups call uri.");
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
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "The listThingGroupsForCoreDevice call failed with response %.*s.",
            (int) response->len,
            response->data
        );
        return ret;
    }

    GG_LOGD(
        "Received response from thingGroups dataplane call: %.*s",
        (int) response->len,
        response->data
    );

    return GG_ERR_OK;
}

static GgError generate_resolve_component_candidates_body(
    GgBuffer component_name,
    GgBuffer component_requirements,
    GgByteVec *body_vec,
    GgArena *alloc
) {
    GgObject architecture_detail_read_value;
    GgError ret = ggl_gg_config_read(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("platformOverride"),
            GG_STR("architecture.detail")
        ),
        alloc,
        &architecture_detail_read_value
    );
    if (ret != GG_ERR_OK) {
        GG_LOGD(
            "No architecture.detail found, so not including it in the component candidates search."
        );
        architecture_detail_read_value = gg_obj_buf(GG_STR(""));
    }

    if (gg_obj_type(architecture_detail_read_value) != GG_TYPE_BUF) {
        GG_LOGD(
            "architecture.detail platformOverride in the config is not a buffer, so not including it in the component candidates search"
        );
        architecture_detail_read_value = gg_obj_buf(GG_STR(""));
    }

    // TODO: Support platform attributes for platformOverride configuration
    GgMap platform_attributes = GG_MAP(
        gg_kv(GG_STR("runtime"), gg_obj_buf(GG_STR("aws_nucleus_lite"))),
        gg_kv(GG_STR("os"), gg_obj_buf(GG_STR("linux"))),
        gg_kv(GG_STR("architecture"), gg_obj_buf(get_current_architecture())),
        gg_kv(GG_STR("architecture.detail"), architecture_detail_read_value)
    );

    if (gg_obj_into_buf(architecture_detail_read_value).len == 0) {
        platform_attributes.len -= 1;
    }

    GgMap platform_info = GG_MAP(
        gg_kv(GG_STR("name"), gg_obj_buf(GG_STR("linux"))),
        gg_kv(GG_STR("attributes"), gg_obj_map(platform_attributes))
    );

    GgMap version_requirements_map = GG_MAP(
        gg_kv(GG_STR("requirements"), gg_obj_buf(component_requirements))
    );

    GgMap component_map = GG_MAP(
        gg_kv(GG_STR("componentName"), gg_obj_buf(component_name)),
        gg_kv(
            GG_STR("versionRequirements"), gg_obj_map(version_requirements_map)
        )
    );

    GgList candidates_list = GG_LIST(gg_obj_map(component_map));

    GgMap request_body = GG_MAP(
        gg_kv(GG_STR("componentCandidates"), gg_obj_list(candidates_list)),
        gg_kv(GG_STR("platform"), gg_obj_map(platform_info))
    );

    ret = gg_json_encode(
        gg_obj_map(request_body), priv_byte_vec_writer(body_vec)
    );
    gg_byte_vec_chain_push(&ret, body_vec, '\0');
    if (ret != GG_ERR_OK) {
        GG_LOGE("Error while encoding body for ResolveComponentCandidates call"
        );
        return ret;
    }

    GG_LOGD("Body for call: %s", body_vec->buf.data);

    return GG_ERR_OK;
}

static GgError resolve_component_with_cloud(
    GgBuffer component_name, GgBuffer version_requirements, GgBuffer *response
) {
    static char resolve_candidates_body_buf[2048];
    GgByteVec body_vec = GG_BYTE_VEC(resolve_candidates_body_buf);
    static uint8_t rcc_body_config_read_mem[128];
    GgArena rcc_alloc = gg_arena_init(GG_BUF(rcc_body_config_read_mem));
    GgError ret = generate_resolve_component_candidates_body(
        component_name, version_requirements, &body_vec, &rcc_alloc
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to generate body for resolveComponentCandidates call");
        return ret;
    }

    GgByteVec data_endpoint = GG_BYTE_VEC(config.data_endpoint);
    ret = get_data_endpoint(&data_endpoint);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get dataplane endpoint.");
        return ret;
    }

    GgByteVec region = GG_BYTE_VEC(config.region);
    ret = get_region(&region);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get region.");
        return ret;
    }

    GgByteVec port = GG_BYTE_VEC(config.port);
    ret = get_data_port(&port);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get dataplane port.");
        return ret;
    }

    GgByteVec pkey_path = GG_BYTE_VEC(config.pkey_path);
    ret = get_private_key_path(&pkey_path);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get private key path.");
        return ret;
    }

    GgByteVec cert_path = GG_BYTE_VEC(config.cert_path);
    ret = get_cert_path(&cert_path);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get certificate path.");
        return ret;
    }

    GgByteVec rootca_path = GG_BYTE_VEC(config.rootca_path);
    ret = get_rootca_path(&rootca_path);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to get certificate path.");
        return ret;
    }

    CertificateDetails cert_details
        = { .gghttplib_cert_path = config.cert_path,
            .gghttplib_root_ca_path = config.rootca_path,
            .gghttplib_p_key_path = config.pkey_path };

    ret = gg_dataplane_call(
        data_endpoint.buf,
        port.buf,
        GG_STR("greengrass/v2/resolveComponentCandidates"),
        cert_details,
        resolve_candidates_body_buf,
        response
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Cloud resolution for the component failed with response %.*s.",
            (int) response->len,
            response->data
        );
        return ret;
    }

    GG_LOGD(
        "Received response from resolveComponentCandidates: %.*s",
        (int) response->len,
        response->data
    );

    return GG_ERR_OK;
}

static GgError parse_dataplane_response_and_save_recipe(
    GgBuffer dataplane_response,
    GglDeploymentHandlerThreadArgs *args,
    GgBuffer *cloud_version
) {
    GgObject json_candidates_response_obj;
    // TODO: Figure out a better size. This response can be big.
    uint8_t candidates_response_mem[100 * sizeof(GgObject)];
    GgArena alloc = gg_arena_init(GG_BUF(candidates_response_mem));
    GgError ret = gg_json_decode_destructive(
        dataplane_response, &alloc, &json_candidates_response_obj
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Error when parsing resolveComponentCandidates response to json."
        );
        return ret;
    }

    if (gg_obj_type(json_candidates_response_obj) != GG_TYPE_MAP) {
        GG_LOGE("resolveComponentCandidates response did not parse into a map."
        );
        return ret;
    }

    GgObject *resolved_component_versions;
    if (!gg_map_get(
            gg_obj_into_map(json_candidates_response_obj),
            GG_STR("resolvedComponentVersions"),
            &resolved_component_versions
        )) {
        GG_LOGE("Missing resolvedComponentVersions.");
        return ret;
    }
    if (gg_obj_type(*resolved_component_versions) != GG_TYPE_LIST) {
        GG_LOGE("resolvedComponentVersions response is not a list.");
        return ret;
    }

    bool first_component = true;
    GG_LIST_FOREACH (
        resolved_version, gg_obj_into_list(*resolved_component_versions)
    ) {
        if (!first_component) {
            GG_LOGE(
                "resolveComponentCandidates returned information for more than one component."
            );
            return GG_ERR_INVALID;
        }
        first_component = false;

        if (gg_obj_type(*resolved_version) != GG_TYPE_MAP) {
            GG_LOGE("Resolved version is not of type map.");
            return ret;
        }

        GgObject *cloud_component_arn_obj;
        GgObject *cloud_component_name_obj;
        GgObject *cloud_component_version_obj;
        GgObject *vendor_guidance_obj;
        GgObject *recipe_obj;

        ret = gg_map_validate(
            gg_obj_into_map(*resolved_version),
            GG_MAP_SCHEMA(
                { GG_STR("arn"),
                  GG_REQUIRED,
                  GG_TYPE_BUF,
                  &cloud_component_arn_obj },
                { GG_STR("componentName"),
                  GG_REQUIRED,
                  GG_TYPE_BUF,
                  &cloud_component_name_obj },
                { GG_STR("componentVersion"),
                  GG_REQUIRED,
                  GG_TYPE_BUF,
                  &cloud_component_version_obj },
                { GG_STR("vendorGuidance"),
                  GG_OPTIONAL,
                  GG_TYPE_BUF,
                  &vendor_guidance_obj },
                { GG_STR("recipe"), GG_REQUIRED, GG_TYPE_BUF, &recipe_obj },
            )
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
        GgBuffer cloud_component_arn
            = gg_obj_into_buf(*cloud_component_arn_obj);
        GgBuffer cloud_component_name
            = gg_obj_into_buf(*cloud_component_name_obj);
        GgBuffer cloud_component_version
            = gg_obj_into_buf(*cloud_component_version_obj);
        GgBuffer recipe_file_content = gg_obj_into_buf(*recipe_obj);

        assert(cloud_component_version.len <= NAME_MAX);

        memcpy(
            cloud_version->data,
            cloud_component_version.data,
            cloud_component_version.len
        );
        cloud_version->len = cloud_component_version.len;

        if (vendor_guidance_obj != NULL) {
            if (gg_buffer_eq(
                    gg_obj_into_buf(*vendor_guidance_obj),
                    GG_STR("DISCONTINUED")
                )) {
                GG_LOGW(
                    "The component version has been discontinued by its publisher. You can deploy this component version, but we recommend that you use a different version of this component"
                );
            }
        }

        if (recipe_file_content.len == 0) {
            GG_LOGE("Recipe is empty.");
        }

        bool decoded = gg_base64_decode_in_place(&recipe_file_content);
        if (!decoded) {
            GG_LOGE("Failed to decode recipe base64.");
            return GG_ERR_PARSE;
        }
        recipe_file_content.data[recipe_file_content.len] = '\0';

        GG_LOGD(
            "Decoded recipe data as: %.*s",
            (int) recipe_file_content.len,
            recipe_file_content.data
        );

        static uint8_t recipe_name_buf[PATH_MAX];
        GgByteVec recipe_name_vec = GG_BYTE_VEC(recipe_name_buf);
        ret = gg_byte_vec_append(&recipe_name_vec, cloud_component_name);
        gg_byte_vec_chain_append(&ret, &recipe_name_vec, GG_STR("-"));
        gg_byte_vec_chain_append(
            &ret, &recipe_name_vec, cloud_component_version
        );
        gg_byte_vec_chain_append(&ret, &recipe_name_vec, GG_STR(".json"));
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to create recipe file name.");
            return ret;
        }

        static uint8_t recipe_dir_buf[PATH_MAX];
        GgByteVec recipe_dir_vec = GG_BYTE_VEC(recipe_dir_buf);
        ret = gg_byte_vec_append(
            &recipe_dir_vec,
            gg_buffer_from_null_term((char *) args->root_path.data)
        );
        gg_byte_vec_chain_append(
            &ret, &recipe_dir_vec, GG_STR("/packages/recipes/")
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to create recipe directory name.");
            return ret;
        }

        {
            // Write file
            int root_dir_fd = -1;
            ret = gg_dir_open(recipe_dir_vec.buf, O_PATH, true, &root_dir_fd);
            if (ret != GG_ERR_OK) {
                GG_LOGE("Failed to open dir when writing cloud recipe.");
                return ret;
            }
            GG_CLEANUP(cleanup_close, root_dir_fd);

            int fd = -1;
            ret = gg_file_openat(
                root_dir_fd,
                recipe_name_vec.buf,
                O_CREAT | O_WRONLY | O_TRUNC,
                (mode_t) 0644,
                &fd
            );
            if (ret != GG_ERR_OK) {
                GG_LOGE(
                    "Failed to open file at the dir when writing cloud recipe."
                );
                return ret;
            }
            GG_CLEANUP(cleanup_close, fd);

            ret = gg_file_write(fd, recipe_file_content);
            if (ret != GG_ERR_OK) {
                GG_LOGE("Write to cloud recipe file failed");
                return ret;
            }
        }

        GG_LOGD("Saved recipe under the name %s", recipe_name_vec.buf.data);

        ret = ggl_gg_config_write(
            GG_BUF_LIST(GG_STR("services"), cloud_component_name, ),
            gg_obj_map(
                GG_MAP(gg_kv(GG_STR("arn"), gg_obj_buf(cloud_component_arn)))
            ),
            &(int64_t) { 1 }
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Write of arn to config failed");
            return ret;
        }
    }

    return GG_ERR_OK;
}

static GgError parse_thing_groups_list(
    GgBuffer list_thing_groups_response,
    GgArena *alloc,
    GgObject **thing_groups_list
) {
    // TODO: Add a schema and only parse the fields we need to save memory
    GgObject json_thing_groups_object;
    GgError ret = gg_json_decode_destructive(
        list_thing_groups_response, alloc, &json_thing_groups_object
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Error when parsing listThingGroups response to json.");
        return ret;
    }

    if (gg_obj_type(json_thing_groups_object) != GG_TYPE_MAP) {
        GG_LOGE("listThingGroups response did not parse into a map.");
        return ret;
    }

    if (!gg_map_get(
            gg_obj_into_map(json_thing_groups_object),
            GG_STR("thingGroups"),
            thing_groups_list
        )) {
        GG_LOGE("Missing thingGroups.");
        return ret;
    }
    if (gg_obj_type(**thing_groups_list) != GG_TYPE_LIST) {
        GG_LOGE("thingGroups response is not a list.");
        return ret;
    }

    return GG_ERR_OK;
}

static GgError add_thing_groups_list_to_config(GgObject *thing_groups_list) {
    GgError ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("DeploymentService"),
            GG_STR("lastThingGroupsListFromCloud")
        ),
        *thing_groups_list,
        &(int64_t) { 1 }
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Write of lastThingGroupsListFromCloud to config failed");
        return ret;
    }

    return GG_ERR_OK;
}

static GgError resolve_dependencies(
    GgMap root_components,
    GgBuffer thing_group_name,
    GglDeploymentType deployment_type,
    GglDeploymentHandlerThreadArgs *args,
    GgArena *alloc,
    GgKVVec *resolved_components_kv_vec
) {
    GgError ret;

    // TODO: Decide on size
    GgKVVec components_to_resolve = GG_KV_VEC((GgKV[64]) { 0 });

    static uint8_t version_requirements_mem[2048] = { 0 };
    GgArena version_requirements_alloc
        = gg_arena_init(GG_BUF(version_requirements_mem));

    // Root components from current deployment
    GG_MAP_FOREACH (pair, root_components) {
        if (gg_obj_type(*gg_kv_val(pair)) != GG_TYPE_MAP) {
            GG_LOGE("Incorrect formatting for deployment components field.");
            return GG_ERR_INVALID;
        }

        GgObject *val;
        GgBuffer component_version = { 0 };
        if (gg_map_get(
                gg_obj_into_map(*gg_kv_val(pair)), GG_STR("version"), &val
            )) {
            if (gg_obj_type(*val) != GG_TYPE_BUF) {
                GG_LOGE("Received invalid argument.");
                return GG_ERR_INVALID;
            }
            component_version = gg_obj_into_buf(*val);
        }

        if (gg_buffer_eq(
                gg_kv_key(*pair), GG_STR("aws.greengrass.NucleusLite")
            )) {
            GgBuffer software_version = GG_STR(GGL_VERSION);
            if (!gg_buffer_eq(component_version, software_version)) {
                GG_LOGE(
                    "The deployment failed. The aws.greengrass.NucleusLite component version specified in the deployment is %.*s, but the version of the GG Lite software is %.*s. Please ensure that the version in the deployment matches before attempting the deployment again.",
                    (int) component_version.len,
                    component_version.data,
                    (int) software_version.len,
                    software_version.data
                );
                return GG_ERR_INVALID;
            }
        }

        ret = gg_kv_vec_push(
            &components_to_resolve,
            gg_kv(gg_kv_key(*pair), gg_obj_buf(component_version))
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    // At this point, components_to_resolve should be only a map of root
    // component names to their version requirements from the deployment. This
    // may be empty! We delete the key first in case components were removed.
    ret = ggl_gg_config_delete(GG_BUF_LIST(
        GG_STR("services"),
        GG_STR("DeploymentService"),
        GG_STR("thingGroupsToRootComponents"),
        thing_group_name
    ));

    if (ret != GG_ERR_OK) {
        GG_LOGW(
            "Error while deleting thing group to root components mapping for thing group %.*s",
            (int) thing_group_name.len,
            thing_group_name.data
        );
        return ret;
    }
    ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("DeploymentService"),
            GG_STR("thingGroupsToRootComponents"),
            thing_group_name
        ),
        gg_obj_map(components_to_resolve.map),
        0
    );

    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to write thing group to root components map to ggconfigd."
        );
        return ret;
    }

    // Get list of thing groups
    static uint8_t list_thing_groups_response_buf[2048] = { 0 };
    GgBuffer list_thing_groups_response
        = GG_BUF(list_thing_groups_response_buf);

    GgObject *thing_groups_list = NULL;
    GgObject empty_list_obj = gg_obj_list(GG_LIST());
    uint8_t thing_groups_response_mem[100 * sizeof(GgObject)];
    GgArena thing_groups_json_alloc
        = gg_arena_init(GG_BUF(thing_groups_response_mem));

    // TODO: Retry infinitely for cloud deployment
    ret = get_device_thing_groups(&list_thing_groups_response);
    if (ret == GG_ERR_OK) {
        ret = parse_thing_groups_list(
            list_thing_groups_response,
            &thing_groups_json_alloc,
            &thing_groups_list
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Error when parsing listThingGroups response for thing groups"
            );
            return ret;
        }
        ret = add_thing_groups_list_to_config(thing_groups_list);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Error updating config with the thing groups list");
            return ret;
        }
    } else {
        if (deployment_type != LOCAL_DEPLOYMENT) {
            GG_LOGE(
                "Cloud call to list thing groups failed. Cloud deployment requires an updated thing group list."
            );
            return ret;
        }
        GG_LOGI(
            "Cloud call to list thing groups failed. Using previous thing groups list as deployment is local."
        );
        ret = ggl_gg_config_read(
            GG_BUF_LIST(
                GG_STR("services"),
                GG_STR("DeploymentService"),
                GG_STR("lastThingGroupsListFromCloud")
            ),
            alloc,
            thing_groups_list
        );
        if (ret != GG_ERR_OK) {
            GG_LOGI(
                "No info found in config for thing groups list, assuming no thing group memberships."
            );
            thing_groups_list = &empty_list_obj;
        }
    }

    GG_LIST_FOREACH (thing_group_item, gg_obj_into_list(*thing_groups_list)) {
        if (gg_obj_type(*thing_group_item) != GG_TYPE_MAP) {
            GG_LOGE("Thing group item is not of type map.");
            return ret;
        }

        GgObject *thing_group_name_from_item_obj;

        ret = gg_map_validate(
            gg_obj_into_map(*thing_group_item),
            GG_MAP_SCHEMA(
                { GG_STR("thingGroupName"),
                  GG_REQUIRED,
                  GG_TYPE_BUF,
                  &thing_group_name_from_item_obj },
            )
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
        GgBuffer thing_group_name_from_item
            = gg_obj_into_buf(*thing_group_name_from_item_obj);

        if (!gg_buffer_eq(thing_group_name_from_item, thing_group_name)) {
            GgObject group_root_components_read_value;
            ret = ggl_gg_config_read(
                GG_BUF_LIST(
                    GG_STR("services"),
                    GG_STR("DeploymentService"),
                    GG_STR("thingGroupsToRootComponents"),
                    thing_group_name_from_item
                ),
                alloc,
                &group_root_components_read_value
            );
            if (ret != GG_ERR_OK) {
                GG_LOGI(
                    "No info found in config for root components for thing group %.*s, assuming no components are part of this thing group.",
                    (int) thing_group_name_from_item.len,
                    thing_group_name_from_item.data
                );
            } else {
                if (gg_obj_type(group_root_components_read_value)
                    != GG_TYPE_MAP) {
                    GG_LOGE(
                        "Did not read a map from config for thing group to root components map"
                    );
                    return GG_ERR_INVALID;
                }

                GG_MAP_FOREACH (
                    root_component_pair,
                    gg_obj_into_map(group_root_components_read_value)
                ) {
                    GgBuffer root_component_val
                        = gg_obj_into_buf(*gg_kv_val(root_component_pair));

                    // If component is already in the root component list, it
                    // must be the same version as the one already in the list
                    // or we have a conflict.
                    GgObject *existing_root_component_version_obj;
                    ret = gg_map_validate(
                        components_to_resolve.map,
                        GG_MAP_SCHEMA(
                            { gg_kv_key(*root_component_pair),
                              GG_OPTIONAL,
                              GG_TYPE_BUF,
                              &existing_root_component_version_obj },
                        )
                    );
                    if (ret != GG_ERR_OK) {
                        return ret;
                    }

                    bool need_to_add_root_component = true;

                    if (existing_root_component_version_obj != NULL) {
                        GgBuffer existing_root_component_version
                            = gg_obj_into_buf(
                                *existing_root_component_version_obj
                            );
                        if (gg_buffer_eq(
                                existing_root_component_version,
                                gg_obj_into_buf(*gg_kv_val(root_component_pair))
                            )) {
                            need_to_add_root_component = false;
                        } else {
                            GG_LOGE(
                                "There is a version conflict for component %.*s, where two deployments are asking for versions %.*s and %.*s. Please check that this root component does not have conflicting versions across your deployments.",
                                (int) gg_kv_key(*root_component_pair).len,
                                gg_kv_key(*root_component_pair).data,
                                (int) root_component_val.len,
                                root_component_val.data,
                                (int) existing_root_component_version.len,
                                existing_root_component_version.data
                            );
                            return GG_ERR_INVALID;
                        }
                    }

                    if (need_to_add_root_component) {
                        GgBuffer root_component_name_buf
                            = gg_kv_key(*root_component_pair);
                        ret = gg_arena_claim_buf(
                            &root_component_name_buf, alloc
                        );
                        if (ret != GG_ERR_OK) {
                            return ret;
                        }

                        GgBuffer root_component_version_buf
                            = root_component_val;
                        ret = gg_arena_claim_buf(
                            &root_component_version_buf,
                            &version_requirements_alloc
                        );
                        if (ret != GG_ERR_OK) {
                            return ret;
                        }

                        ret = gg_kv_vec_push(
                            &components_to_resolve,
                            gg_kv(
                                root_component_name_buf,
                                gg_obj_buf(root_component_version_buf)
                            )
                        );
                        if (ret != GG_ERR_OK) {
                            return ret;
                        }

                        GG_LOGD(
                            "Added %.*s to the list of root components to resolve from the thing group %.*s",
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

    // Add local components to components to resolve, if the deployment is not
    // targeting LOCAL_DEPLOYMENTS
    if (!gg_buffer_eq(GG_STR("LOCAL_DEPLOYMENTS"), thing_group_name)) {
        GgObject local_components_read_value;
        ret = ggl_gg_config_read(
            GG_BUF_LIST(
                GG_STR("services"),
                GG_STR("DeploymentService"),
                GG_STR("thingGroupsToRootComponents"),
                GG_STR("LOCAL_DEPLOYMENTS")
            ),
            alloc,
            &local_components_read_value
        );
        if (ret != GG_ERR_OK) {
            GG_LOGI(
                "No local components found in config, proceeding deployment without needing to add local components."
            );
        } else {
            if (gg_obj_type(local_components_read_value) != GG_TYPE_MAP) {
                GG_LOGE(
                    "Did not read a map from config while looking up local components."
                );
                return GG_ERR_INVALID;
            }

            GG_MAP_FOREACH (
                root_component_pair,
                gg_obj_into_map(local_components_read_value)
            ) {
                GgBuffer root_component_val
                    = gg_obj_into_buf(*gg_kv_val(root_component_pair));

                // If component is already in the root component list, it
                // must be the same version as the one already in the list
                // or we have a conflict.
                GgObject *existing_root_component_version_obj;
                ret = gg_map_validate(
                    components_to_resolve.map,
                    GG_MAP_SCHEMA(
                        { gg_kv_key(*root_component_pair),
                          GG_OPTIONAL,
                          GG_TYPE_BUF,
                          &existing_root_component_version_obj },
                    )
                );
                if (ret != GG_ERR_OK) {
                    return ret;
                }

                bool need_to_add_root_component = true;

                if (existing_root_component_version_obj != NULL) {
                    GgBuffer existing_root_component_version
                        = gg_obj_into_buf(*existing_root_component_version_obj);
                    if (gg_buffer_eq(
                            existing_root_component_version, root_component_val
                        )) {
                        need_to_add_root_component = false;
                    } else {
                        GG_LOGE(
                            "There is a version conflict for component %.*s, where it is already locally deployed as version %.*s and the deployment requests version %.*s.",
                            (int) gg_kv_key(*root_component_pair).len,
                            gg_kv_key(*root_component_pair).data,
                            (int) root_component_val.len,
                            root_component_val.data,
                            (int) existing_root_component_version.len,
                            existing_root_component_version.data
                        );
                        return GG_ERR_INVALID;
                    }
                }

                if (need_to_add_root_component) {
                    GgBuffer root_component_name_buf
                        = gg_kv_key(*root_component_pair);
                    ret = gg_arena_claim_buf(&root_component_name_buf, alloc);
                    if (ret != GG_ERR_OK) {
                        return ret;
                    }

                    GgBuffer root_component_version_buf = root_component_val;
                    ret = gg_arena_claim_buf(
                        &root_component_version_buf, &version_requirements_alloc
                    );
                    if (ret != GG_ERR_OK) {
                        return ret;
                    }

                    ret = gg_kv_vec_push(
                        &components_to_resolve,
                        gg_kv(
                            root_component_name_buf,
                            gg_obj_buf(root_component_version_buf)
                        )
                    );
                    GG_LOGD(
                        "Added %.*s to the list of root components to resolve as it has been previously locally deployed.",
                        (int) root_component_name_buf.len,
                        root_component_name_buf.data
                    );
                }
            }
        }
    }

    GG_MAP_FOREACH (pair, components_to_resolve.map) {
        GgBuffer pair_val = gg_obj_into_buf(*gg_kv_val(pair));

        // We assume that we have not resolved a component yet if we are finding
        // it in this map.
        uint8_t resolved_version_arr[NAME_MAX];
        GgBuffer resolved_version = GG_BUF(resolved_version_arr);
        bool found_local_candidate = resolve_component_version(
            gg_kv_key(*pair), pair_val, &resolved_version
        );

        if (!found_local_candidate) {
            // Resolve with cloud and download recipe
            static uint8_t resolve_component_candidates_response_buf[16384]
                = { 0 };
            GgBuffer resolve_component_candidates_response
                = GG_BUF(resolve_component_candidates_response_buf);

            ret = resolve_component_with_cloud(
                gg_kv_key(*pair),
                pair_val,
                &resolve_component_candidates_response
            );
            if (ret != GG_ERR_OK) {
                return ret;
            }

            bool is_empty_response = gg_buffer_eq(
                resolve_component_candidates_response, GG_STR("{}")
            );

            if (is_empty_response) {
                GG_LOGI(
                    "Cloud version resolution failed for component %.*s.",
                    (int) gg_kv_key(*pair).len,
                    pair_val.data
                );
                return GG_ERR_FAILURE;
            }

            ret = parse_dataplane_response_and_save_recipe(
                resolve_component_candidates_response, args, &resolved_version
            );
            if (ret != GG_ERR_OK) {
                return ret;
            }
        }

        // Add resolved component to list of resolved components
        ret = gg_arena_claim_buf(&resolved_version, alloc);
        if (ret != GG_ERR_OK) {
            return ret;
        }

        ret = gg_kv_vec_push(
            resolved_components_kv_vec,
            gg_kv(gg_kv_key(*pair), gg_obj_buf(resolved_version))
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Error while adding component to list of resolved component"
            );
            return ret;
        }

        // Find dependencies from recipe and add them to the list of components
        // to resolve. If the dependency is for a component that is already
        // resolved, verify that new requirements are satisfied and fail
        // deployment if not.

        // Get actual recipe read
        GgObject recipe_obj;
        static uint8_t recipe_mem[GGL_COMPONENT_RECIPE_MAX_LEN] = { 0 };
        GgArena recipe_alloc = gg_arena_init(GG_BUF(recipe_mem));
        ret = ggl_recipe_get_from_file(
            args->root_path_fd,
            gg_kv_key(*pair),
            resolved_version,
            &recipe_alloc,
            &recipe_obj
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
        GgObject *component_dependencies;

        if (gg_obj_type(recipe_obj) != GG_TYPE_MAP) {
            GG_LOGE("Recipe object did not parse into a map.");
            return GG_ERR_INVALID;
        }

        ret = gg_map_validate(
            gg_obj_into_map(recipe_obj),
            GG_MAP_SCHEMA(
                { GG_STR("ComponentDependencies"),
                  GG_OPTIONAL,
                  GG_TYPE_MAP,
                  &component_dependencies },
            )
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
        if (component_dependencies != NULL) {
            GG_MAP_FOREACH (
                dependency, gg_obj_into_map(*component_dependencies)
            ) {
                if (gg_obj_type(*gg_kv_val(dependency)) != GG_TYPE_MAP) {
                    GG_LOGE(
                        "Component dependency in recipe does not have map data"
                    );
                    return GG_ERR_INVALID;
                }

                // If the component is aws.greengrass.Nucleus or
                // aws.greengrass.TokenExchangeService or aws.greengrass.Cli
                // ignore it and never add it as a dependency to check or parse.
                if (gg_buffer_eq(
                        gg_kv_key(*dependency), GG_STR("aws.greengrass.Nucleus")
                    )
                    || gg_buffer_eq(
                        gg_kv_key(*dependency),
                        GG_STR("aws.greengrass.TokenExchangeService")
                    )
                    || gg_buffer_eq(
                        gg_kv_key(*dependency), GG_STR("aws.greengrass.Cli")
                    )) {
                    GG_LOGD(
                        "Skipping a dependency during resolution as it is %.*s",
                        (int) gg_kv_key(*dependency).len,
                        gg_kv_key(*dependency).data
                    );
                    continue;
                }

                GgObject *dep_version_requirement_obj = NULL;
                ret = gg_map_validate(
                    gg_obj_into_map(*gg_kv_val(dependency)),
                    GG_MAP_SCHEMA(
                        { GG_STR("VersionRequirement"),
                          GG_REQUIRED,
                          GG_TYPE_BUF,
                          &dep_version_requirement_obj },
                    )
                );
                if (ret != GG_ERR_OK) {
                    return ret;
                }
                GgBuffer dep_version_requirement
                    = gg_obj_into_buf(*dep_version_requirement_obj);

                // If we already resolved the component version, check that it
                // still satisfies the new requirement and fail otherwise.
                GgObject *already_resolved_version;
                ret = gg_map_validate(
                    resolved_components_kv_vec->map,
                    GG_MAP_SCHEMA(
                        { gg_kv_key(*dependency),
                          GG_OPTIONAL,
                          GG_TYPE_BUF,
                          &already_resolved_version },
                    )
                );
                if (ret != GG_ERR_OK) {
                    return ret;
                }
                if (already_resolved_version != NULL) {
                    bool meets_requirements = is_in_range(
                        gg_obj_into_buf(*already_resolved_version),
                        dep_version_requirement
                    );
                    if (!meets_requirements) {
                        GG_LOGE(
                            "Already resolved component does not meet new dependency requirement, failing dependency resolution."
                        );
                        return GG_ERR_FAILURE;
                    }
                }

                if (!already_resolved_version) {
                    // If we haven't resolved it yet, check if we have an
                    // existing requirement and append the new requirement if
                    // so.
                    GgObject *existing_requirements;
                    ret = gg_map_validate(
                        components_to_resolve.map,
                        GG_MAP_SCHEMA(
                            { gg_kv_key(*dependency),
                              GG_OPTIONAL,
                              GG_TYPE_BUF,
                              &existing_requirements },
                        )
                    );
                    if (ret != GG_ERR_OK) {
                        return ret;
                    }
                    if (existing_requirements != NULL) {
                        uint8_t new_req_buf[PATH_MAX];
                        GgByteVec new_req_vec = GG_BYTE_VEC(new_req_buf);
                        ret = gg_byte_vec_append(
                            &new_req_vec,
                            gg_obj_into_buf(*existing_requirements)
                        );
                        gg_byte_vec_chain_push(&ret, &new_req_vec, ' ');
                        gg_byte_vec_chain_append(
                            &ret, &new_req_vec, dep_version_requirement
                        );
                        if (ret != GG_ERR_OK) {
                            GG_LOGE(
                                "Failed to create new requirements for dependency version."
                            );
                            return ret;
                        }

                        uint8_t *new_req = GG_ARENA_ALLOCN(
                            &version_requirements_alloc,
                            uint8_t,
                            new_req_vec.buf.len
                        );
                        if (new_req == NULL) {
                            GG_LOGE(
                                "Ran out of memory while trying to create new requirements"
                            );
                            return GG_ERR_NOMEM;
                        }

                        memcpy(
                            new_req, new_req_vec.buf.data, new_req_vec.buf.len
                        );
                        *existing_requirements = gg_obj_buf((GgBuffer
                        ) { .data = new_req, .len = new_req_vec.buf.len });
                    }

                    // If we haven't resolved it yet, and it doesn't have an
                    // existing requirement, add it.
                    if (!existing_requirements) {
                        GgBuffer name_key_buf = gg_kv_key(*dependency);
                        ret = gg_arena_claim_buf(&name_key_buf, alloc);
                        if (ret != GG_ERR_OK) {
                            return ret;
                        }

                        GgBuffer vers_key_buf = dep_version_requirement;
                        ret = gg_arena_claim_buf(
                            &vers_key_buf, &version_requirements_alloc
                        );
                        if (ret != GG_ERR_OK) {
                            return ret;
                        }

                        ret = gg_kv_vec_push(
                            &components_to_resolve,
                            gg_kv(name_key_buf, gg_obj_buf(vers_key_buf))
                        );
                        if (ret != GG_ERR_OK) {
                            return ret;
                        }
                    }
                }
            }
        }
    }
    return GG_ERR_OK;
}

static GgError open_component_artifacts_dir(
    int artifact_store_fd,
    GgBuffer component_name,
    GgBuffer component_version,
    int *version_fd
) {
    int component_fd = -1;
    GgError ret = gg_dir_openat(
        artifact_store_fd, component_name, O_PATH, true, &component_fd
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }
    GG_CLEANUP(cleanup_close, component_fd);
    return gg_dir_openat(
        component_fd, component_version, O_PATH, true, version_fd
    );
}

static GgBuffer get_unversioned_substring(GgBuffer arn) {
    size_t colon_index = SIZE_MAX;
    for (size_t i = arn.len; i > 0; i--) {
        if (arn.data[i - 1] == ':') {
            colon_index = i - 1;
            break;
        }
    }
    return gg_buffer_substr(arn, 0, colon_index);
}

static GgError add_arn_list_to_config(
    GgBuffer component_name, GgBuffer configuration_arn
) {
    GG_LOGD(
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
         + (sizeof(GgObject) * MAX_DEPLOYMENT_TARGETS)];
    GgArena arn_list_alloc = gg_arena_init(GG_BUF(arn_list_mem));

    GgObject arn_list_obj;
    GgError ret = ggl_gg_config_read(
        GG_BUF_LIST(GG_STR("services"), component_name, GG_STR("configArn")),
        &arn_list_alloc,
        &arn_list_obj
    );

    if ((ret != GG_ERR_OK) && (ret != GG_ERR_NOENTRY)) {
        GG_LOGE("Failed to retrieve configArn.");
        return GG_ERR_FAILURE;
    }

    GgObjVec new_arn_list
        = GG_OBJ_VEC((GgObject[MAX_DEPLOYMENT_TARGETS]) { 0 });
    if (ret != GG_ERR_NOENTRY) {
        // list exists in config, parse for current config arn and append if it
        // is not already included
        if (gg_obj_type(arn_list_obj) != GG_TYPE_LIST) {
            GG_LOGE("Configuration arn list not of expected type.");
            return GG_ERR_INVALID;
        }

        GgList arn_list = gg_obj_into_list(arn_list_obj);
        if (arn_list.len >= MAX_DEPLOYMENT_TARGETS) {
            GG_LOGE(
                "Cannot append configArn: Component is deployed as part of too many deployments (%zu >= %zu).",
                arn_list.len,
                (size_t) MAX_DEPLOYMENT_TARGETS
            );
            return GG_ERR_FAILURE;
        }
        GG_LIST_FOREACH (arn, arn_list) {
            if (gg_obj_type(*arn) != GG_TYPE_BUF) {
                GG_LOGE("Configuration arn not of type buffer.");
                return ret;
            }
            if (gg_buffer_eq(
                    get_unversioned_substring(gg_obj_into_buf(*arn)),
                    get_unversioned_substring(configuration_arn)
                )) {
                // arn for this group already added to config, replace it
                GG_LOGD(
                    "Configuration arn already exists for this thing group, overwriting it."
                );
                *arn = gg_obj_buf(configuration_arn);
                ret = ggl_gg_config_write(
                    GG_BUF_LIST(
                        GG_STR("services"), component_name, GG_STR("configArn")
                    ),
                    gg_obj_list(arn_list),
                    &(int64_t) { 3 }
                );
                if (ret != GG_ERR_OK) {
                    GG_LOGE(
                        "Failed to write configuration arn list to the config."
                    );
                    return ret;
                }
                return GG_ERR_OK;
            }
            ret = gg_obj_vec_push(&new_arn_list, *arn);
            assert(ret == GG_ERR_OK);
        }
    }

    ret = gg_obj_vec_push(&new_arn_list, gg_obj_buf(configuration_arn));
    assert(ret == GG_ERR_OK);

    ret = ggl_gg_config_write(
        GG_BUF_LIST(GG_STR("services"), component_name, GG_STR("configArn")),
        gg_obj_list(new_arn_list.list),
        &(int64_t) { 3 }
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to write configuration arn list to the config.");
        return ret;
    }

    return GG_ERR_OK;
}

static GgError send_fss_update(
    GglDeployment *deployment, bool deployment_succeeded
) {
    GgBuffer server = GG_STR("gg_fleet_status");
    static uint8_t buffer[10 * sizeof(GgObject)] = { 0 };

    // TODO: Fill out statusDetails and unchangedRootComponents
    GgMap status_details_map = GG_MAP(
        gg_kv(
            GG_STR("detailedStatus"),
            gg_obj_buf(
                deployment_succeeded ? GG_STR("SUCCESSFUL")
                                     : GG_STR("FAILED_ROLLBACK_NOT_REQUESTED")
            )
        ),
    );

    GgMap deployment_info = GG_MAP(
        gg_kv(
            GG_STR("status"),
            gg_obj_buf(
                deployment_succeeded ? GG_STR("SUCCEEDED") : GG_STR("FAILED")
            )
        ),
        gg_kv(
            GG_STR("fleetConfigurationArnForStatus"),
            gg_obj_buf(deployment->configuration_arn)
        ),
        gg_kv(GG_STR("deploymentId"), gg_obj_buf(deployment->deployment_id)),
        gg_kv(GG_STR("statusDetails"), gg_obj_map(status_details_map)),
        gg_kv(GG_STR("unchangedRootComponents"), gg_obj_list(GG_LIST())),
    );

    uint8_t trigger_buffer[24];
    GgBuffer trigger = GG_BUF(trigger_buffer);

    if (deployment->type == LOCAL_DEPLOYMENT) {
        trigger = GG_STR("LOCAL_DEPLOYMENT");
    } else if (deployment->type == THING_GROUP_DEPLOYMENT) {
        trigger = GG_STR("THING_GROUP_DEPLOYMENT");
    }

    GgMap args = GG_MAP(
        gg_kv(GG_STR("trigger"), gg_obj_buf(trigger)),
        gg_kv(GG_STR("deployment_info"), gg_obj_map(deployment_info))
    );

    GgArena alloc = gg_arena_init(GG_BUF(buffer));
    GgObject result;

    GgError ret = ggl_call(
        server, GG_STR("send_fleet_status_update"), args, NULL, &alloc, &result
    );

    if (ret != 0) {
        GG_LOGE(
            "Failed to send send_fleet_status_update to fleet status service: %d.",
            ret
        );
        return ret;
    }

    return GG_ERR_OK;
}

static GgError deployment_status_callback(void *ctx, GgObject data) {
    (void) ctx;
    if (gg_obj_type(data) != GG_TYPE_MAP) {
        GG_LOGE("Result is not a map.");
        return GG_ERR_INVALID;
    }
    GgObject *component_name_obj;
    GgObject *status_obj;
    GgError ret = gg_map_validate(
        gg_obj_into_map(data),
        GG_MAP_SCHEMA(
            { GG_STR("component_name"),
              GG_REQUIRED,
              GG_TYPE_BUF,
              &component_name_obj },
            { GG_STR("lifecycle_state"), GG_REQUIRED, GG_TYPE_BUF, &status_obj }
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Unexpected gghealthd response format.");
        return GG_ERR_INVALID;
    }
    GgBuffer component_name = gg_obj_into_buf(*component_name_obj);
    GgBuffer status = gg_obj_into_buf(*status_obj);

    if (gg_buffer_eq(status, GG_STR("BROKEN"))) {
        GG_LOGE(
            "%.*s is broken.", (int) component_name.len, component_name.data
        );
        return GG_ERR_FAILURE;
    }
    if (gg_buffer_eq(status, GG_STR("RUNNING"))
        || gg_buffer_eq(status, GG_STR("FINISHED"))) {
        GG_LOGD("Component succeeded.");
        return GG_ERR_OK;
    }
    GG_LOGE("Unexpected lifecycle state %.*s", (int) status.len, status.data);
    return GG_ERR_INVALID;
}

static GgError wait_for_phase_status(GgBufVec component_vec, GgBuffer phase) {
    // TODO: hack
    (void) gg_sleep(5);

    for (size_t i = 0; i < component_vec.buf_list.len; i++) {
        // Add .[phase name] into the component name
        static uint8_t full_comp_name_mem[PATH_MAX];
        GgByteVec full_comp_name_vec = GG_BYTE_VEC(full_comp_name_mem);
        GgError ret = gg_byte_vec_append(
            &full_comp_name_vec, component_vec.buf_list.bufs[i]
        );
        gg_byte_vec_chain_push(&ret, &full_comp_name_vec, '.');
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to push '.' character to component name vector.");
            return ret;
        }
        ret = gg_byte_vec_append(&full_comp_name_vec, phase);
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Failed to generate %*.s phase name for %*.scomponent.",
                (int) phase.len,
                phase.data,
                (int) component_vec.buf_list.bufs[i].len,
                component_vec.buf_list.bufs[i].data
            );
            return ret;
        }
        GG_LOGD(
            "Awaiting %.*s to finish.",
            (int) full_comp_name_vec.buf.len,
            full_comp_name_vec.buf.data
        );

        ret = ggl_sub_response(
            GG_STR("gg_health"),
            GG_STR("subscribe_to_lifecycle_completion"),
            GG_MAP(gg_kv(
                GG_STR("component_name"), gg_obj_buf(full_comp_name_vec.buf)
            )),
            deployment_status_callback,
            NULL,
            NULL,
            300
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Failed waiting for %.*s",
                (int) full_comp_name_vec.buf.len,
                full_comp_name_vec.buf.data
            );
            return GG_ERR_FAILURE;
        }
    }
    return GG_ERR_OK;
}

static GgError wait_for_deployment_status(GgMap resolved_components) {
    GG_LOGT("Beginning wait for deployment completion");
    // TODO: hack
    (void) gg_sleep(5);

    GG_MAP_FOREACH (component, resolved_components) {
        GG_LOGD(
            "Waiting for %.*s to finish",
            (int) gg_kv_key(*component).len,
            gg_kv_key(*component).data
        );
        GgError ret = ggl_sub_response(
            GG_STR("gg_health"),
            GG_STR("subscribe_to_lifecycle_completion"),
            GG_MAP(gg_kv(
                GG_STR("component_name"), gg_obj_buf(gg_kv_key(*component))
            )),
            deployment_status_callback,
            NULL,
            NULL,
            300
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Failed waiting for %.*s",
                (int) gg_kv_key(*component).len,
                gg_kv_key(*component).data
            );
            return GG_ERR_FAILURE;
        }
    }
    return GG_ERR_OK;
}

static GgError validate_iot_endpoint(
    GgBuffer endpoint, GgBuffer name, GgBuffer device_region
) {
    if (endpoint.len == 0) {
        GG_LOGE("%.*s is empty.", (int) name.len, name.data);
        return GG_ERR_INVALID;
    }

    // Check region consistency for AWS endpoints
    if ((gg_buffer_contains(endpoint, GG_STR(".amazonaws."), NULL))
        && (!gg_buffer_contains(endpoint, device_region, NULL))) {
        GG_LOGE(
            "%.*s region does not match device region %.*s.",
            (int) name.len,
            name.data,
            (int) device_region.len,
            device_region.data
        );
        return GG_ERR_INVALID;
    }

    return GG_ERR_OK;
}

/// Read a NucleusLite config string. Returns empty buffer on failure.
static GgBuffer read_nucleus_config_str(GgBuffer key, GgArena *alloc) {
    GgBuffer val = { 0 };
    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            key
        ),
        alloc,
        &val
    );
    if (ret != GG_ERR_OK) {
        return (GgBuffer) { 0 };
    }
    return val;
}

/// Get the NucleusLite configurationUpdate merge map from a deployment's
/// components. Returns GG_ERR_OK with *merge set to NULL if not present.
/// Returns GG_ERR_INVALID if present but malformed.
static GgError get_nucleus_merge_map(
    GgMap components, GgMap *merge, bool *found
) {
    *found = false;

    GgObject *nucleus_info = NULL;
    if (!gg_map_get(
            components, GG_STR("aws.greengrass.NucleusLite"), &nucleus_info
        )) {
        return GG_ERR_OK;
    }
    if (gg_obj_type(*nucleus_info) != GG_TYPE_MAP) {
        GG_LOGE("NucleusLite component info is not a map.");
        return GG_ERR_INVALID;
    }

    GgObject *config_update = NULL;
    if (!gg_map_get(
            gg_obj_into_map(*nucleus_info),
            GG_STR("configurationUpdate"),
            &config_update
        )) {
        return GG_ERR_OK;
    }
    if (gg_obj_type(*config_update) != GG_TYPE_MAP) {
        GG_LOGE("configurationUpdate is not a map.");
        return GG_ERR_INVALID;
    }

    GgObject *merge_obj = NULL;
    if (!gg_map_get(
            gg_obj_into_map(*config_update), GG_STR("merge"), &merge_obj
        )) {
        return GG_ERR_OK;
    }
    if (gg_obj_type(*merge_obj) != GG_TYPE_MAP) {
        GG_LOGE("configurationUpdate merge is not a map.");
        return GG_ERR_INVALID;
    }

    *merge = gg_obj_into_map(*merge_obj);
    *found = true;
    return GG_ERR_OK;
}

static GgError validate_endpoint_switch_deployment(GgMap components) {
    GgMap merge = { 0 };
    bool found = false;
    GgError ret = get_nucleus_merge_map(components, &merge, &found);
    if ((ret != GG_ERR_OK) || !found) {
        return ret;
    }

    GgObject *region_obj = NULL;
    GgObject *data_ep_obj = NULL;
    GgObject *cred_ep_obj = NULL;
    gg_map_get(merge, GG_STR("awsRegion"), &region_obj);
    gg_map_get(merge, GG_STR("iotDataEndpoint"), &data_ep_obj);
    gg_map_get(merge, GG_STR("iotCredEndpoint"), &cred_ep_obj);

    // Only validate if endpoint or region is being changed
    if ((region_obj == NULL) && (data_ep_obj == NULL)
        && (cred_ep_obj == NULL)) {
        return GG_ERR_OK;
    }

    // Determine effective values: deployment value if present, else config
    GgBuffer effective_region;
    static uint8_t region_buf[24] = { 0 };
    GgArena region_alloc = gg_arena_init(GG_BUF(region_buf));
    if (region_obj != NULL) {
        if (gg_obj_type(*region_obj) != GG_TYPE_BUF) {
            GG_LOGE("awsRegion is not a string.");
            return GG_ERR_INVALID;
        }
        effective_region = gg_obj_into_buf(*region_obj);
    } else {
        effective_region
            = read_nucleus_config_str(GG_STR("awsRegion"), &region_alloc);
    }
    if (effective_region.len == 0) {
        GG_LOGE("Cannot validate endpoints: failed to read device region.");
        return GG_ERR_FAILURE;
    }

    GgBuffer effective_data_ep;
    static uint8_t data_ep_buf[128] = { 0 };
    GgArena data_ep_alloc = gg_arena_init(GG_BUF(data_ep_buf));
    if (data_ep_obj != NULL) {
        if (gg_obj_type(*data_ep_obj) != GG_TYPE_BUF) {
            GG_LOGE("iotDataEndpoint is not a string.");
            return GG_ERR_INVALID;
        }
        effective_data_ep = gg_obj_into_buf(*data_ep_obj);
    } else {
        effective_data_ep = read_nucleus_config_str(
            GG_STR("iotDataEndpoint"), &data_ep_alloc
        );
    }

    GgBuffer effective_cred_ep;
    static uint8_t cred_ep_buf[128] = { 0 };
    GgArena cred_ep_alloc = gg_arena_init(GG_BUF(cred_ep_buf));
    if (cred_ep_obj != NULL) {
        if (gg_obj_type(*cred_ep_obj) != GG_TYPE_BUF) {
            GG_LOGE("iotCredEndpoint is not a string.");
            return GG_ERR_INVALID;
        }
        effective_cred_ep = gg_obj_into_buf(*cred_ep_obj);
    } else {
        effective_cred_ep = read_nucleus_config_str(
            GG_STR("iotCredEndpoint"), &cred_ep_alloc
        );
    }

    if (effective_data_ep.len > 0) {
        ret = validate_iot_endpoint(
            effective_data_ep, GG_STR("iotDataEndpoint"), effective_region
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    if (effective_cred_ep.len > 0) {
        ret = validate_iot_endpoint(
            effective_cred_ep, GG_STR("iotCredEndpoint"), effective_region
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    return GG_ERR_OK;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void handle_deployment(
    GglDeployment *deployment,
    GglDeploymentHandlerThreadArgs *args,
    bool *deployment_succeeded
) {
    int root_path_fd = args->root_path_fd;

    // Validate IoT endpoint deployment before any state changes
    GgError ret = validate_endpoint_switch_deployment(deployment->components);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Deployment rejected: IoT endpoint validation failed.");
        return;
    }

    if (deployment->recipe_directory_path.len != 0) {
        ret = merge_dir_to(
            deployment->recipe_directory_path, "packages/recipes/"
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to copy recipes.");
            return;
        }
    }

    if (deployment->artifacts_directory_path.len != 0) {
        ret = merge_dir_to(
            deployment->artifacts_directory_path, "packages/artifacts/"
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to copy artifacts.");
            return;
        }
    }

    GgKVVec resolved_components_kv_vec = GG_KV_VEC((GgKV[64]) { 0 });
    static uint8_t resolve_dependencies_mem[8192] = { 0 };
    GgArena resolve_dependencies_alloc
        = gg_arena_init(GG_BUF(resolve_dependencies_mem));
    ret = resolve_dependencies(
        deployment->components,
        deployment->thing_group,
        deployment->type,
        args,
        &resolve_dependencies_alloc,
        &resolved_components_kv_vec
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to do dependency resolution for deployment, failing deployment."
        );
        return;
    }

    GgByteVec region = GG_BYTE_VEC(config.region);
    ret = get_region(&region);
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to get region from config.");
    }
    CertificateDetails iot_credentials
        = { .gghttplib_cert_path = config.cert_path,
            .gghttplib_p_key_path = config.pkey_path,
            .gghttplib_root_ca_path = config.rootca_path };

    TesCredentials tes_credentials = { .aws_region = region.buf };
    ret = get_tes_credentials(&tes_credentials);
    bool tes_creds_retrieved = (ret == GG_ERR_OK);
    if (!tes_creds_retrieved) {
        GG_LOGW(
            "Failed to retrieve TES credentials, attempting to complete deployment without TES credentials."
        );
    }

    int artifact_store_fd = -1;
    ret = gg_dir_openat(
        root_path_fd,
        GG_STR("packages/artifacts"),
        O_PATH,
        true,
        &artifact_store_fd
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to open artifact store");
        return;
    }
    GG_CLEANUP(cleanup_close, artifact_store_fd);

    int artifact_archive_fd = -1;
    ret = gg_dir_openat(
        root_path_fd,
        GG_STR("packages/artifacts-unarchived"),
        O_PATH,
        true,
        &artifact_archive_fd
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to open archive store.");
        return;
    }
    GG_CLEANUP(cleanup_close, artifact_archive_fd);

    GglDigest digest_context = ggl_new_digest(&ret);
    if (ret != GG_ERR_OK) {
        return;
    }
    GG_CLEANUP(ggl_free_digest, digest_context);

    // list of {component name -> component version} for all new components in
    // the deployment
    GgKVVec components_to_deploy = GG_KV_VEC((GgKV[64]) { 0 });

    GG_MAP_FOREACH (pair, resolved_components_kv_vec.map) {
        GgBuffer pair_val = gg_obj_into_buf(*gg_kv_val(pair));

        // check config to see if component has completed processing
        GgArena resp_alloc = gg_arena_init(GG_BUF((uint8_t[128]) { 0 }));
        GgBuffer resp;

        ret = ggl_gg_config_read_str(
            GG_BUF_LIST(
                GG_STR("services"),
                GG_STR("DeploymentService"),
                GG_STR("deploymentState"),
                GG_STR("components"),
                gg_kv_key(*pair)
            ),
            &resp_alloc,
            &resp
        );
        if (ret == GG_ERR_OK) {
            GG_LOGD(
                "Component %.*s completed processing in previous run. Will not be reprocessed.",
                (int) gg_kv_key(*pair).len,
                gg_kv_key(*pair).data
            );
            continue;
        }

        // check config to see if bootstrap steps have already been run for this
        // component
        if (component_bootstrap_phase_completed(gg_kv_key(*pair))) {
            GG_LOGD(
                "Bootstrap component %.*s encountered. Bootstrap phase has already been completed. Adding to list of components to process to complete any other lifecycle stages.",
                (int) gg_kv_key(*pair).len,
                gg_kv_key(*pair).data
            );
            ret = gg_kv_vec_push(
                &components_to_deploy, gg_kv(gg_kv_key(*pair), *gg_kv_val(pair))
            );
            if (ret != GG_ERR_OK) {
                GG_LOGE(
                    "Failed to add component info for %.*s to deployment vector.",
                    (int) gg_kv_key(*pair).len,
                    gg_kv_key(*pair).data
                );
                return;
            }
            continue;
        }

        int component_artifacts_fd = -1;
        ret = open_component_artifacts_dir(
            artifact_store_fd,
            gg_kv_key(*pair),
            pair_val,
            &component_artifacts_fd
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to open artifact directory.");
            return;
        }
        GG_CLEANUP(cleanup_close, component_artifacts_fd);
        int component_archive_dir_fd = -1;
        ret = open_component_artifacts_dir(
            artifact_archive_fd,
            gg_kv_key(*pair),
            pair_val,
            &component_archive_dir_fd
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to open unarchived artifacts directory.");
            return;
        }
        GG_CLEANUP(cleanup_close, component_archive_dir_fd);
        GgObject recipe_obj;
        static uint8_t recipe_mem[GGL_COMPONENT_RECIPE_MAX_LEN] = { 0 };
        GgArena alloc = gg_arena_init(GG_BUF(recipe_mem));
        ret = ggl_recipe_get_from_file(
            args->root_path_fd, gg_kv_key(*pair), pair_val, &alloc, &recipe_obj
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to validate and decode recipe");
            return;
        }

        // TODO: See if there is a better requirement. If a customer has the
        // same version as before but somehow updated their component
        // version their component may not get the updates.
        bool component_updated = true;

        static uint8_t old_component_version_mem[128] = { 0 };
        alloc = gg_arena_init(GG_BUF(old_component_version_mem));
        GgBuffer old_component_version;
        ret = ggl_gg_config_read_str(
            GG_BUF_LIST(
                GG_STR("services"), gg_kv_key(*pair), GG_STR("version")
            ),
            &alloc,
            &old_component_version
        );
        if (ret != GG_ERR_OK) {
            GG_LOGD(
                "Failed to get component version from config, assuming component is new."
            );
        } else {
            if (gg_buffer_eq(pair_val, old_component_version)) {
                GG_LOGD(
                    "Detected that component %.*s has not changed version.",
                    (int) gg_kv_key(*pair).len,
                    gg_kv_key(*pair).data
                );
                component_updated = false;
            }
        }

        static uint8_t component_arn_buffer[256];
        alloc = gg_arena_init(GG_BUF(component_arn_buffer));
        GgBuffer component_arn;
        GgError arn_ret = ggl_gg_config_read_str(
            GG_BUF_LIST(GG_STR("services"), gg_kv_key(*pair), GG_STR("arn")),
            &alloc,
            &component_arn
        );
        if (arn_ret != GG_ERR_OK) {
            // TODO: Check over artifacts list even if local deployment and
            // attempt download if needed
            GG_LOGW(
                "Failed to retrieve arn. Assuming recipe artifacts are found on-disk."
            );
        } else if (!component_updated) {
            // TODO: Check artifact hashes to see if artifacts have changed/need
            // to be redownloaded
            GG_LOGD(
                "Not retrieving component artifacts as the version has not changed."
            );
        } else if (!tes_creds_retrieved) {
            if (deployment->type != LOCAL_DEPLOYMENT) {
                GG_LOGE(
                    "TES credentials were not retrieved and deployment is not a local deployment. Unable to do artifact retrieval."
                );
                return;
            }
            GG_LOGW(
                "TES credentials were not retrieved, but deployment is local. Skipping artifact retrieval for component %.*s and attempting to complete deployment.",
                (int) gg_kv_key(*pair).len,
                gg_kv_key(*pair).data
            );
        } else {
            ret = get_recipe_artifacts(
                component_arn,
                tes_credentials,
                iot_credentials,
                gg_obj_into_map(recipe_obj),
                component_artifacts_fd,
                component_archive_dir_fd,
                digest_context
            );
            if (ret != GG_ERR_OK) {
                GG_LOGE("Failed to get artifacts from recipe.");
                return;
            }
        }

        ret = ggl_gg_config_write(
            GG_BUF_LIST(
                GG_STR("services"), gg_kv_key(*pair), GG_STR("version")
            ),
            *gg_kv_val(pair),
            &(int64_t) { 0 }
        );

        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Failed to write version of %.*s to ggconfigd.",
                (int) gg_kv_key(*pair).len,
                gg_kv_key(*pair).data
            );
            return;
        }

        ret = add_arn_list_to_config(
            gg_kv_key(*pair), deployment->configuration_arn
        );

        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Failed to write configuration arn of %.*s to ggconfigd.",
                (int) gg_kv_key(*pair).len,
                gg_kv_key(*pair).data
            );
            return;
        }

        ret = apply_configurations(
            deployment, gg_kv_key(*pair), GG_STR("reset")
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Failed to apply reset configuration update for %.*s.",
                (int) gg_kv_key(*pair).len,
                gg_kv_key(*pair).data
            );
            return;
        }

        GgObject *intermediate_obj;
        GgObject *default_config_obj;

        if (gg_map_get(
                gg_obj_into_map(recipe_obj),
                GG_STR("ComponentConfiguration"),
                &intermediate_obj
            )) {
            if (gg_obj_type(*intermediate_obj) != GG_TYPE_MAP) {
                GG_LOGE("ComponentConfiguration is not a map type");
                return;
            }

            if (gg_map_get(
                    gg_obj_into_map(*intermediate_obj),
                    GG_STR("DefaultConfiguration"),
                    &default_config_obj
                )) {
                ret = ggl_gg_config_write(
                    GG_BUF_LIST(
                        GG_STR("services"),
                        gg_kv_key(*pair),
                        GG_STR("configuration")
                    ),
                    *default_config_obj,
                    &(int64_t) { 0 }
                );

                if (ret != GG_ERR_OK) {
                    GG_LOGE("Failed to send default config to ggconfigd.");
                    return;
                }
            } else {
                GG_LOGI(
                    "DefaultConfiguration not found in the recipe of %.*s.",
                    (int) gg_kv_key(*pair).len,
                    gg_kv_key(*pair).data
                );
            }
        } else {
            GG_LOGI(
                "ComponentConfiguration not found in the recipe of %.*s.",
                (int) gg_kv_key(*pair).len,
                gg_kv_key(*pair).data
            );
        }

        ret = apply_configurations(
            deployment, gg_kv_key(*pair), GG_STR("merge")
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Failed to apply merge configuration update for %.*s.",
                (int) gg_kv_key(*pair).len,
                gg_kv_key(*pair).data
            );
            return;
        }

        static uint8_t recipe_runner_path_buf[PATH_MAX];
        GgByteVec recipe_runner_path_vec = GG_BYTE_VEC(recipe_runner_path_buf);
        ret = gg_byte_vec_append(
            &recipe_runner_path_vec,
            gg_buffer_from_null_term((char *) args->bin_path)
        );
        gg_byte_vec_chain_append(
            &ret, &recipe_runner_path_vec, GG_STR("recipe-runner")
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to create recipe runner path.");
            return;
        }

        char *posix_user = NULL;
        ret = get_posix_user(&posix_user);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to get posix_user.");
            return;
        }
        if (strlen(posix_user) < 1) {
            GG_LOGE("Run with default posix user is not set.");
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

        recipe2unit_args.component_name = gg_kv_key(*pair);
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

        GgObject recipe_buff_obj;
        GgObject *component_name;
        static uint8_t unit_convert_alloc_mem[GGL_COMPONENT_RECIPE_MAX_LEN];
        GgArena unit_convert_alloc
            = gg_arena_init(GG_BUF(unit_convert_alloc_mem));
        HasPhase phases = { 0 };
        GgError err = convert_to_unit(
            &recipe2unit_args,
            &unit_convert_alloc,
            &recipe_buff_obj,
            &component_name,
            &phases
        );

        if (err != GG_ERR_OK) {
            return;
        }

        if (!gg_buffer_eq(gg_obj_into_buf(*component_name), gg_kv_key(*pair))) {
            GG_LOGE(
                "Component name from recipe does not match component name from recipe file."
            );
            return;
        }

        if (component_updated) {
            ret = gg_kv_vec_push(
                &components_to_deploy, gg_kv(gg_kv_key(*pair), *gg_kv_val(pair))
            );
            if (ret != GG_ERR_OK) {
                GG_LOGE(
                    "Failed to add component info for %.*s to deployment vector.",
                    (int) gg_kv_key(*pair).len,
                    gg_kv_key(*pair).data
                );
                return;
            }
            GG_LOGD(
                "Added %.*s to list of components that need to be processed.",
                (int) gg_kv_key(*pair).len,
                gg_kv_key(*pair).data
            );
        } else {
            // component already exists, check its lifecycle state
            GgArena component_status_alloc
                = gg_arena_init(GG_BUF((uint8_t[NAME_MAX]) { 0 }));
            GgBuffer component_status;
            ret = ggl_gghealthd_retrieve_component_status(
                gg_kv_key(*pair), &component_status_alloc, &component_status
            );

            if (ret != GG_ERR_OK) {
                GG_LOGD(
                    "Failed to retrieve health status for %.*s. Redeploying component.",
                    (int) gg_kv_key(*pair).len,
                    gg_kv_key(*pair).data
                );
                ret = gg_kv_vec_push(
                    &components_to_deploy,
                    gg_kv(gg_kv_key(*pair), *gg_kv_val(pair))
                );
                if (ret != GG_ERR_OK) {
                    GG_LOGE(
                        "Failed to add component info for %.*s to deployment vector.",
                        (int) gg_kv_key(*pair).len,
                        gg_kv_key(*pair).data
                    );
                    return;
                }
                GG_LOGD(
                    "Added %.*s to list of components that need to be processed.",
                    (int) gg_kv_key(*pair).len,
                    gg_kv_key(*pair).data
                );
            }

            // Skip redeploying components in a RUNNING state
            if (gg_buffer_eq(component_status, GG_STR("RUNNING"))
                || gg_buffer_eq(component_status, GG_STR("FINISHED"))) {
                GG_LOGD(
                    "Component %.*s is already running. Will not redeploy.",
                    (int) gg_kv_key(*pair).len,
                    gg_kv_key(*pair).data
                );
                // save as a deployed component in case of bootstrap
                ret = save_component_info(
                    gg_kv_key(*pair), pair_val, GG_STR("completed")
                );
                if (ret != GG_ERR_OK) {
                    return;
                }
            } else {
                ret = gg_kv_vec_push(
                    &components_to_deploy,
                    gg_kv(gg_kv_key(*pair), *gg_kv_val(pair))
                );
                if (ret != GG_ERR_OK) {
                    GG_LOGE(
                        "Failed to add component info for %.*s to deployment vector.",
                        (int) gg_kv_key(*pair).len,
                        gg_kv_key(*pair).data
                    );
                    return;
                }
                GG_LOGD(
                    "Added %.*s to list of components that need to be processed.",
                    (int) gg_kv_key(*pair).len,
                    gg_kv_key(*pair).data
                );
            }
        }
    }

    // TODO: Add a logic to only run the phases that exist with the latest
    // deployment
    if (components_to_deploy.map.len != 0) {
        // collect all component names that have relevant bootstrap service
        // files
        static GgBuffer bootstrap_comp_name_buf[MAX_COMP_NAME_BUF_SIZE];
        GgBufVec bootstrap_comp_name_buf_vec
            = GG_BUF_VEC(bootstrap_comp_name_buf);

        ret = process_bootstrap_phase(
            components_to_deploy.map,
            args->root_path,
            &bootstrap_comp_name_buf_vec,
            deployment
        );
        if (ret != GG_ERR_OK) {
            return;
        }

        // wait for all the bootstrap status
        ret = wait_for_phase_status(
            bootstrap_comp_name_buf_vec, GG_STR("bootstrap")
        );
        if (ret != GG_ERR_OK) {
            return;
        }

        // collect all component names that have relevant install service
        // files
        static GgBuffer install_comp_name_buf[MAX_COMP_NAME_BUF_SIZE];
        GgBufVec install_comp_name_buf_vec = GG_BUF_VEC(install_comp_name_buf);

        // process all install files
        GG_MAP_FOREACH (component, components_to_deploy.map) {
            GgBuffer component_name = gg_kv_key(*component);

            static uint8_t install_service_file_path_buf[PATH_MAX];
            GgByteVec install_service_file_path_vec
                = GG_BYTE_VEC(install_service_file_path_buf);
            ret = gg_byte_vec_append(
                &install_service_file_path_vec, args->root_path
            );
            gg_byte_vec_chain_append(
                &ret, &install_service_file_path_vec, GG_STR("/")
            );
            gg_byte_vec_chain_append(
                &ret, &install_service_file_path_vec, GG_STR("ggl.")
            );
            gg_byte_vec_chain_append(
                &ret, &install_service_file_path_vec, component_name
            );
            gg_byte_vec_chain_append(
                &ret, &install_service_file_path_vec, GG_STR(".install.service")
            );
            if (ret == GG_ERR_OK) {
                // check if the current component name has relevant install
                // service file created
                int fd = -1;
                ret = gg_file_open(
                    install_service_file_path_vec.buf, O_RDONLY, 0, &fd
                );
                if (ret != GG_ERR_OK) {
                    GG_LOGD(
                        "Component %.*s does not have the relevant install service file",
                        (int) component_name.len,
                        component_name.data
                    );
                } else { // relevant install service file exists
                    GG_CLEANUP(cleanup_close, fd);
                    (void) disable_and_unlink_service(&component_name, INSTALL);
                    // add relevant component name into the vector
                    ret = gg_buf_vec_push(
                        &install_comp_name_buf_vec, component_name
                    );
                    if (ret != GG_ERR_OK) {
                        GG_LOGE(
                            "Failed to add the install component name into vector"
                        );
                        return;
                    }

                    // initiate link command for 'install'
                    static uint8_t link_command_buf[PATH_MAX];
                    GgByteVec link_command_vec = GG_BYTE_VEC(link_command_buf);
                    ret = gg_byte_vec_append(
                        &link_command_vec, GG_STR("systemctl link ")
                    );
                    gg_byte_vec_chain_append(
                        &ret,
                        &link_command_vec,
                        install_service_file_path_vec.buf
                    );
                    gg_byte_vec_chain_push(&ret, &link_command_vec, '\0');
                    if (ret != GG_ERR_OK) {
                        GG_LOGE(
                            "Failed to create systemctl link command for:%.*s",
                            (int) install_service_file_path_vec.buf.len,
                            install_service_file_path_vec.buf.data
                        );
                        return;
                    }

                    GG_LOGD(
                        "Command to execute: %.*s",
                        (int) link_command_vec.buf.len,
                        link_command_vec.buf.data
                    );

                    // NOLINTBEGIN(concurrency-mt-unsafe)
                    int system_ret = system((char *) link_command_vec.buf.data);
                    if (WIFEXITED(system_ret)) {
                        if (WEXITSTATUS(system_ret) != 0) {
                            GG_LOGE(
                                "systemctl link failed for:%.*s",
                                (int) install_service_file_path_vec.buf.len,
                                install_service_file_path_vec.buf.data
                            );
                            return;
                        }
                        GG_LOGI(
                            "systemctl link exited for %.*s with child status %d\n",
                            (int) install_service_file_path_vec.buf.len,
                            install_service_file_path_vec.buf.data,
                            WEXITSTATUS(system_ret)
                        );
                    } else {
                        GG_LOGE(
                            "systemctl link did not exit normally for %.*s",
                            (int) install_service_file_path_vec.buf.len,
                            install_service_file_path_vec.buf.data
                        );
                        return;
                    }

                    // initiate start command for 'install' (non-blocking to
                    // allow wait_for_phase_status to handle retries via
                    // gghealthd)
                    static uint8_t start_command_buf[PATH_MAX];
                    GgByteVec start_command_vec
                        = GG_BYTE_VEC(start_command_buf);
                    ret = gg_byte_vec_append(
                        &start_command_vec,
                        GG_STR("systemctl start --no-block ")
                    );
                    gg_byte_vec_chain_append(
                        &ret, &start_command_vec, GG_STR("ggl.")
                    );
                    gg_byte_vec_chain_append(
                        &ret, &start_command_vec, component_name
                    );
                    gg_byte_vec_chain_append(
                        &ret, &start_command_vec, GG_STR(".install.service\0")
                    );

                    GG_LOGD(
                        "Command to execute: %.*s",
                        (int) start_command_vec.buf.len,
                        start_command_vec.buf.data
                    );
                    if (ret != GG_ERR_OK) {
                        GG_LOGE(
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
                            GG_LOGE(
                                "systemctl start --no-block failed for %.*s",
                                (int) install_service_file_path_vec.buf.len,
                                install_service_file_path_vec.buf.data
                            );
                            return;
                        }
                        GG_LOGI(
                            "systemctl start --no-block exited with child status %d\n",
                            WEXITSTATUS(system_ret)
                        );
                    } else {
                        GG_LOGE(
                            "systemctl start --no-block did not exit normally for %.*s",
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
            install_comp_name_buf_vec, GG_STR("install")
        );
        if (ret != GG_ERR_OK) {
            return;
        }

        // process all run or startup files after install only
        GG_MAP_FOREACH (component, components_to_deploy.map) {
            GgBuffer component_name = gg_kv_key(*component);
            GgBuffer component_version = gg_obj_into_buf(*gg_kv_val(component));

            static uint8_t service_file_path_buf[PATH_MAX];
            GgByteVec service_file_path_vec
                = GG_BYTE_VEC(service_file_path_buf);
            ret = gg_byte_vec_append(&service_file_path_vec, args->root_path);
            gg_byte_vec_chain_append(&ret, &service_file_path_vec, GG_STR("/"));
            gg_byte_vec_chain_append(
                &ret, &service_file_path_vec, GG_STR("ggl.")
            );
            gg_byte_vec_chain_append(
                &ret, &service_file_path_vec, component_name
            );
            gg_byte_vec_chain_append(
                &ret, &service_file_path_vec, GG_STR(".service")
            );
            if (ret == GG_ERR_OK) {
                // check if the current component name has relevant run
                // service file created
                int fd = -1;
                ret = gg_file_open(service_file_path_vec.buf, O_RDONLY, 0, &fd);
                if (ret != GG_ERR_OK) {
                    GG_LOGD(
                        "Component %.*s does not have the relevant run service file",
                        (int) component_name.len,
                        component_name.data
                    );
                } else {
                    GG_CLEANUP(cleanup_close, fd);
                    (void
                    ) disable_and_unlink_service(&component_name, RUN_STARTUP);
                    // run link command
                    static uint8_t link_command_buf[PATH_MAX];
                    GgByteVec link_command_vec = GG_BYTE_VEC(link_command_buf);
                    ret = gg_byte_vec_append(
                        &link_command_vec, GG_STR("systemctl link ")
                    );
                    gg_byte_vec_chain_append(
                        &ret, &link_command_vec, service_file_path_vec.buf
                    );
                    gg_byte_vec_chain_push(&ret, &link_command_vec, '\0');
                    if (ret != GG_ERR_OK) {
                        GG_LOGE("Failed to create systemctl link command.");
                        return;
                    }

                    GG_LOGD(
                        "Command to execute: %.*s",
                        (int) link_command_vec.buf.len,
                        link_command_vec.buf.data
                    );

                    // NOLINTNEXTLINE(concurrency-mt-unsafe)
                    int system_ret = system((char *) link_command_vec.buf.data);
                    if (WIFEXITED(system_ret)) {
                        if (WEXITSTATUS(system_ret) != 0) {
                            GG_LOGE("systemctl link command failed");
                            return;
                        }
                        GG_LOGI(
                            "systemctl link exited with child status %d\n",
                            WEXITSTATUS(system_ret)
                        );
                    } else {
                        GG_LOGE("systemctl link did not exit normally");
                        return;
                    }

                    // run enable command
                    static uint8_t enable_command_buf[PATH_MAX];
                    GgByteVec enable_command_vec
                        = GG_BYTE_VEC(enable_command_buf);
                    ret = gg_byte_vec_append(
                        &enable_command_vec, GG_STR("systemctl enable ")
                    );
                    gg_byte_vec_chain_append(
                        &ret, &enable_command_vec, service_file_path_vec.buf
                    );
                    gg_byte_vec_chain_push(&ret, &enable_command_vec, '\0');
                    if (ret != GG_ERR_OK) {
                        GG_LOGE("Failed to create systemctl enable command.");
                        return;
                    }
                    GG_LOGD(
                        "Command to execute: %.*s",
                        (int) enable_command_vec.buf.len,
                        enable_command_vec.buf.data
                    );

                    // NOLINTNEXTLINE(concurrency-mt-unsafe)
                    system_ret = system((char *) enable_command_vec.buf.data);
                    if (WIFEXITED(system_ret)) {
                        if (WEXITSTATUS(system_ret) != 0) {
                            GG_LOGE("systemctl enable failed");
                            return;
                        }
                        GG_LOGI(
                            "systemctl enable exited with child status %d\n",
                            WEXITSTATUS(system_ret)
                        );
                    } else {
                        GG_LOGE("systemctl enable did not exit normally");
                        return;
                    }
                }
            }

            // save as a deployed component in case of bootstrap
            ret = save_component_info(
                component_name, component_version, GG_STR("completed")
            );
            if (ret != GG_ERR_OK) {
                return;
            }
        }

        // run daemon-reload command once all the files are linked
        static uint8_t reload_command_buf[PATH_MAX];
        GgByteVec reload_command_vec = GG_BYTE_VEC(reload_command_buf);
        ret = gg_byte_vec_append(
            &reload_command_vec, GG_STR("systemctl daemon-reload\0")
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to create systemctl daemon-reload command.");
            return;
        }
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        int system_ret = system((char *) reload_command_vec.buf.data);
        if (WIFEXITED(system_ret)) {
            if (WEXITSTATUS(system_ret) != 0) {
                GG_LOGE("systemctl daemon-reload failed");
                return;
            }
            GG_LOGI(
                "systemctl daemon-reload exited with child status %d\n",
                WEXITSTATUS(system_ret)
            );
        } else {
            GG_LOGE("systemctl daemon-reload did not exit normally");
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
    if (ret != GG_ERR_OK) {
        return;
    }

    GG_LOGI("Performing cleanup of stale components");
    ret = cleanup_stale_versions(resolved_components_kv_vec.map);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Error while cleaning up stale components after deployment.");
    }

    *deployment_succeeded = true;
}

static GgError ggl_deployment_listen(GglDeploymentHandlerThreadArgs *args) {
    // check for in progress deployment in case of bootstrap
    GglDeployment bootstrap_deployment = { 0 };
    uint8_t jobs_id_resp_mem[64] = { 0 };
    GgBuffer jobs_id = GG_BUF(jobs_id_resp_mem);
    int64_t jobs_version = 0;

    GgError ret = retrieve_in_progress_deployment(
        &bootstrap_deployment, &jobs_id, &jobs_version
    );
    if (ret != GG_ERR_OK) {
        GG_LOGD("No deployments previously in progress detected.");
    } else {
        GG_LOGI(
            "Found previously in progress deployment %.*s. Resuming deployment.",
            (int) bootstrap_deployment.deployment_id.len,
            bootstrap_deployment.deployment_id.data
        );

        bool send_deployment_update
            = (GG_ERR_OK
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
            GG_LOGI(
                "Completed deployment processing and reporting job as SUCCEEDED."
            );
            (void) update_current_jobs_deployment(
                bootstrap_deployment.deployment_id, GG_STR("SUCCEEDED")
            );
        } else if (send_deployment_update) {
            GG_LOGW(
                "Completed deployment processing and reporting job as FAILED."
            );
            (void) update_current_jobs_deployment(
                bootstrap_deployment.deployment_id, GG_STR("FAILED")
            );
        } else {
            GG_LOGI("Completed deployment, but job was canceled.");
        }
        // clear any potential saved deployment info for next deployment
        ret = delete_saved_deployment_from_config();
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to delete saved deployment info from config.");
        }

        // TODO: investigate deployment queue behavior with bootstrap deployment
        ggl_deployment_release(&bootstrap_deployment);
    }

    while (true) {
        GglDeployment *deployment;
        // Since this is blocking, error is fatal
        ret = ggl_deployment_dequeue(&deployment);
        if (ret != GG_ERR_OK) {
            return ret;
        }

        GG_LOGI("Processing incoming deployment.");

        (void) update_current_jobs_deployment(
            deployment->deployment_id, GG_STR("IN_PROGRESS")
        );

        bool deployment_succeeded = false;
        handle_deployment(deployment, args, &deployment_succeeded);

        (void) send_fss_update(deployment, deployment_succeeded);

        // TODO: need error details from handle_deployment
        if (deployment_succeeded) {
            GG_LOGI(
                "Completed deployment processing and reporting job as SUCCEEDED."
            );
            (void) update_current_jobs_deployment(
                deployment->deployment_id, GG_STR("SUCCEEDED")
            );
        } else {
            GG_LOGW(
                "Completed deployment processing and reporting job as FAILED."
            );
            (void) update_current_jobs_deployment(
                deployment->deployment_id, GG_STR("FAILED")
            );
        }
        // clear any potential saved deployment info for next deployment
        ret = delete_saved_deployment_from_config();
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to delete saved deployment info from config.");
        }

        ggl_deployment_release(deployment);
    }
}

void *ggl_deployment_handler_thread(void *ctx) {
    GG_LOGD("Starting deployment processing thread.");

    (void) ggl_deployment_listen(ctx);

    GG_LOGE("Deployment thread exiting due to failure.");

    // clear any potential saved deployment info for next deployment
    GgError ret = delete_saved_deployment_from_config();
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to delete saved deployment info from config.");
    }

    // This is safe as long as only this thread will ever call exit.

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);

    return NULL;
}
