// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "deployment_handler.h"
#include "dependency_resolver.h"
#include "deployment_configuration.h"
#include "deployment_model.h"
#include "deployment_queue.h"
#include "iot_jobs_listener.h"
#include "stale_component.h"
#include <sys/types.h>
#include <assert.h>
#include <fcntl.h>
#include <ggl/alloc.h>
#include <ggl/base64.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/cleanup.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/core_bus/sub_response.h>
#include <ggl/digest.h>
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
#include <ggl/uri.h>
#include <ggl/utils.h>
#include <ggl/vector.h>
#include <ggl/zip.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_RECIPE_BUF_SIZE 256000
#define MAX_DECODE_BUF_LEN 4096
#define MAX_COMP_NAME_BUF_SIZE 10000

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
    GGL_CLEANUP(cleanup_close, source_fd);

    int dest_fd;
    ret = ggl_dir_openat(root_path_fd, subdir, O_RDONLY, true, &dest_fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_CLEANUP(cleanup_close, dest_fd);

    return ggl_copy_dir(source_fd, dest_fd);
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
    int component_archive_store_fd,
    GglDigest digest_context
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
        GglObject *expected_digest = NULL;
        GglObject *algorithm = NULL;

        GglError err = ggl_map_validate(
            artifacts.items[i].map,
            GGL_MAP_SCHEMA(
                { GGL_STR("Uri"), true, GGL_TYPE_BUF, &uri_obj },
                { GGL_STR("Unarchive"), false, GGL_TYPE_BUF, &unarchive_obj },
                { GGL_STR("Digest"), false, GGL_TYPE_BUF, &expected_digest },
                { GGL_STR("Algorithm"), false, GGL_TYPE_BUF, &algorithm }
            )
        );

        if (err != GGL_ERR_OK) {
            GGL_LOGE("Failed to validate recipe artifact");
            return GGL_ERR_PARSE;
        }

        bool needs_verification = false;
        if (expected_digest != NULL) {
            if (algorithm != NULL) {
                if (!ggl_buffer_eq(algorithm->buf, GGL_STR("SHA-256"))) {
                    GGL_LOGE("Unsupported digest algorithm");
                    return GGL_ERR_UNSUPPORTED;
                }
            } else {
                GGL_LOGW("Assuming SHA-256 digest.");
            }

            if (!ggl_base64_decode_in_place(&expected_digest->buf)) {
                GGL_LOGE("Failed to decode digest.");
                return GGL_ERR_PARSE;
            }
            needs_verification = true;
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
                component_store_fd,
                info.file,
                expected_digest->buf,
                digest_context
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

static GglError add_arn_list_to_config(
    GglBuffer component_name, GglBuffer configuration_arn
) {
    // add configuration arn to the config if it is not already present
    // added to the config as a list, this is later used in fss
    GglBuffer arn_list_mem = GGL_BUF((uint8_t[128]) { 0 });
    GglBumpAlloc arn_list_balloc = ggl_bump_alloc_init(arn_list_mem);
    GglObject arn_list;

    GglError ret = ggl_gg_config_read(
        GGL_BUF_LIST(GGL_STR("services"), component_name, GGL_STR("configArn")),
        &arn_list_balloc.alloc,
        &arn_list
    );

    if (ret != GGL_ERR_OK) {
        // no list exists in config, create one
        GglObjVec config_arn_list = GGL_OBJ_VEC((GglObject[10]) { 0 });
        ret = ggl_obj_vec_push(
            &config_arn_list, GGL_OBJ_BUF(configuration_arn)
        );

        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to update configuration arn list.");
            return ret;
        }

        ret = ggl_gg_config_write(
            GGL_BUF_LIST(
                GGL_STR("services"), component_name, GGL_STR("configArn")
            ),
            GGL_OBJ_LIST(config_arn_list.list),
            0
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to write configuration arn list to the config.");
            return ret;
        }
    } else {
        // list exists in config, parse for current config arn and append if it
        // is not already included
        if (arn_list.type != GGL_TYPE_LIST) {
            GGL_LOGE("Configuration arn list not of expected type.");
            return GGL_ERR_INVALID;
        }
        GglObjVec arn_vec = { .list = arn_list.list, .capacity = 10 };
        GGL_LIST_FOREACH(arn, arn_vec.list) {
            if (arn->type != GGL_TYPE_BUF) {
                GGL_LOGE("Configuration arn not of type buffer.");
                return ret;
            }
            if (ggl_buffer_eq(arn->buf, configuration_arn)) {
                // arn already added to config
                return GGL_ERR_OK;
            }
            ret = ggl_obj_vec_push(&arn_vec, GGL_OBJ_BUF(configuration_arn));
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to update configuration arn list.");
                return ret;
            }

            ret = ggl_gg_config_write(
                GGL_BUF_LIST(
                    GGL_STR("services"), component_name, GGL_STR("configArn")
                ),
                GGL_OBJ_LIST(arn_vec.list),
                0
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to write configuration arn list to the config."
                );
                return ret;
            }
        }
    }
    return GGL_ERR_OK;
}

static GglError send_fss_update(GglBuffer trigger) {
    GglBuffer server = GGL_STR("/aws/ggl/gg-fleet-statusd");
    static uint8_t buffer[10 * sizeof(GglObject)] = { 0 };

    GglMap args = GGL_MAP({ GGL_STR("trigger"), GGL_OBJ_BUF(trigger) });

    GglBumpAlloc alloc = ggl_bump_alloc_init(GGL_BUF(buffer));
    GglObject result;

    GglError ret = ggl_call(
        server,
        GGL_STR("send_fleet_status_update"),
        args,
        NULL,
        &alloc.alloc,
        &result
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
    if (data.type != GGL_TYPE_MAP) {
        GGL_LOGE("Result is not a map.");
        return GGL_ERR_INVALID;
    }
    GglObject *component_name = NULL;
    GglObject *status = NULL;
    GglError ret = ggl_map_validate(
        data.map,
        GGL_MAP_SCHEMA(
            { GGL_STR("component_name"), true, GGL_TYPE_BUF, &component_name },
            { GGL_STR("lifecycle_state"), true, GGL_TYPE_BUF, &status }
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Unexpected gghealthd response format.");
        return GGL_ERR_INVALID;
    }

    if (ggl_buffer_eq(status->buf, GGL_STR("BROKEN"))) {
        GGL_LOGE(
            "%.*s is broken.",
            (int) component_name->buf.len,
            component_name->buf.data
        );
        return GGL_ERR_FAILURE;
    }
    if (ggl_buffer_eq(status->buf, GGL_STR("RUNNING"))
        || ggl_buffer_eq(status->buf, GGL_STR("FINISHED"))) {
        GGL_LOGD("Component succeeded.");
        return GGL_ERR_OK;
    }
    GGL_LOGE(
        "Unexpected lifecycle state %.*s",
        (int) status->buf.len,
        status->buf.data
    );
    return GGL_ERR_INVALID;
}

static GglError wait_for_install_status(GglBufVec component_vec) {
    // TODO: hack
    ggl_sleep(5);

    for (size_t i = 0; i < component_vec.buf_list.len; i++) {
        // Add .install into the component name
        static uint8_t install_comp_name[PATH_MAX];
        GglByteVec install_comp_name_vec = GGL_BYTE_VEC(install_comp_name);
        GglError ret = ggl_byte_vec_append(
            &install_comp_name_vec, component_vec.buf_list.bufs[i]
        );
        ggl_byte_vec_append(&install_comp_name_vec, GGL_STR(".install"));
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to generate the install component name.");
            return ret;
        }
        GGL_LOGD(
            "Awaiting %.*s to finish.",
            (int) install_comp_name_vec.buf.len,
            install_comp_name_vec.buf.data
        );

        GglError method_error = GGL_ERR_OK;
        ret = ggl_sub_response(
            GGL_STR("/aws/ggl/gghealthd"),
            GGL_STR("subscribe_to_lifecycle_completion"),
            GGL_MAP({ GGL_STR("component_name"),
                      GGL_OBJ_BUF(install_comp_name_vec.buf) }),
            deployment_status_callback,
            NULL,
            &method_error,
            300
        );
        if ((ret != GGL_ERR_OK) || (method_error != GGL_ERR_OK)) {
            return GGL_ERR_FAILURE;
        }
    }
    return GGL_ERR_OK;
}

static GglError wait_for_deployment_status(GglMap resolved_components) {
    GGL_LOGT("Beginning wait for deployment completion");
    // TODO: hack
    ggl_sleep(5);

    GGL_MAP_FOREACH(component, resolved_components) {
        GGL_LOGD(
            "Waiting for %.*s to finish",
            (int) component->key.len,
            component->key.data
        );
        GglError method_error = GGL_ERR_OK;
        GglError ret = ggl_sub_response(
            GGL_STR("/aws/ggl/gghealthd"),
            GGL_STR("subscribe_to_lifecycle_completion"),
            GGL_MAP({ GGL_STR("component_name"), GGL_OBJ_BUF(component->key) }),
            deployment_status_callback,
            NULL,
            &method_error,
            300
        );
        if ((ret != GGL_ERR_OK) || (method_error != GGL_ERR_OK)) {
            return GGL_ERR_FAILURE;
        }
    }
    return GGL_ERR_OK;
}

// This will be refactored soon with recipe2unit in c, so ignore this warning
// for now
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void handle_deployment(
    GglDeployment *deployment,
    GglDeploymentHandlerThreadArgs *args,
    bool *deployment_succeeded
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

    if (deployment->cloud_root_components_to_add.len != 0) {
        GglKVVec resolved_components_kv_vec = GGL_KV_VEC((GglKV[64]) { 0 });
        static uint8_t resolve_dependencies_mem[8192] = { 0 };
        GglBumpAlloc resolve_dependencies_balloc
            = ggl_bump_alloc_init(GGL_BUF(resolve_dependencies_mem));
        GglError ret = resolve_dependencies(
            &deployment->cloud_root_components_to_add,
            deployment->thing_group,
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

        GglDigest digest_context = ggl_new_digest(&ret);
        if (ret != GGL_ERR_OK) {
            return;
        }
        GGL_CLEANUP(ggl_free_digest, digest_context);

        static GglBuffer comp_name_buf[MAX_COMP_NAME_BUF_SIZE];
        GglBufVec updated_comp_name_vec = GGL_BUF_VEC(comp_name_buf);

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
            GglError arn_ret = ggl_gg_config_read_str(
                GGL_BUF_LIST(GGL_STR("services"), pair->key, GGL_STR("arn")),
                &component_arn
            );
            if (arn_ret != GGL_ERR_OK) {
                GGL_LOGW("Failed to retrieve arn. Assuming recipe artifacts "
                         "are found on-disk.");
            } else {
                ret = get_recipe_artifacts(
                    component_arn,
                    tes_credentials,
                    iot_credentials,
                    recipe_obj.map,
                    component_artifacts_fd,
                    component_archive_dir_fd,
                    digest_context
                );
            }

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

            static Recipe2UnitArgs recipe2unit_args;
            memset(&recipe2unit_args, 0, sizeof(Recipe2UnitArgs));
            recipe2unit_args.user = posix_user;
            recipe2unit_args.group = group;

            recipe2unit_args.component_name = pair->key;
            recipe2unit_args.component_version = pair->val.buf;

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
            recipe2unit_args.root_path_fd = root_path_fd;

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

            bool component_updated = true;

            static uint8_t old_component_version_mem[128] = { 0 };
            GglBuffer old_component_version
                = GGL_BUF(old_component_version_mem);
            ret = ggl_gg_config_read_str(
                GGL_BUF_LIST(
                    GGL_STR("services"), component_name->buf, GGL_STR("version")
                ),
                &old_component_version
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGD("Failed to get component version from config, "
                         "assuming component is new.");
            } else {
                if (ggl_buffer_eq(pair->val.buf, old_component_version)) {
                    GGL_LOGD(
                        "Detected that component %.*s has not changed version.",
                        (int) pair->key.len,
                        pair->key.data
                    );
                    component_updated = false;
                }
            }
            // TODO: See if there is a better requirement. If a customer has the
            // same version as before but somehow updated their component
            // version their component may not get the updates.

            ret = ggl_gg_config_write(
                GGL_BUF_LIST(
                    GGL_STR("services"), component_name->buf, GGL_STR("version")
                ),
                pair->val,
                0
            );

            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to write component version to ggconfigd.");
                return;
            }

            ret = add_arn_list_to_config(
                component_name->buf, deployment->configuration_arn
            );

            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to write configuration arn to ggconfigd.");
                return;
            }

            GglObject *intermediate_obj;
            GglObject *default_config_obj;

            if (ggl_map_get(
                    recipe_buff_obj.map,
                    GGL_STR("ComponentConfiguration"),
                    &intermediate_obj
                )) {
                if (intermediate_obj->type != GGL_TYPE_MAP) {
                    GGL_LOGE("ComponentConfiguration is not a map type");
                    return;
                }

                if (ggl_map_get(
                        intermediate_obj->map,
                        GGL_STR("DefaultConfiguration"),
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

            if (component_updated) {
                ret = ggl_buf_vec_push(&updated_comp_name_vec, pair->key);
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE("Failed to add the component name into vector");
                    return;
                }
            }
        }

        if (updated_comp_name_vec.buf_list.len != 0) {
            // collect all component names that have relevant install service
            // files
            static GglBuffer install_comp_name_buf[MAX_COMP_NAME_BUF_SIZE];
            GglBufVec install_comp_name_buf_vec
                = GGL_BUF_VEC(install_comp_name_buf);

            // process all install files first
            for (size_t i = 0; i < updated_comp_name_vec.buf_list.len; i++) {
                static uint8_t install_service_file_path_buf[PATH_MAX];
                GglByteVec install_service_file_path_vec
                    = GGL_BYTE_VEC(install_service_file_path_buf);
                ret = ggl_byte_vec_append(
                    &install_service_file_path_vec, args->root_path
                );
                ggl_byte_vec_append(
                    &install_service_file_path_vec, GGL_STR("/")
                );
                ggl_byte_vec_append(
                    &install_service_file_path_vec, GGL_STR("ggl.")
                );
                ggl_byte_vec_chain_append(
                    &ret,
                    &install_service_file_path_vec,
                    updated_comp_name_vec.buf_list.bufs[i]
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
                        GGL_LOGW(
                            "Component %.*s does not have the relevant install "
                            "service file",
                            (int) updated_comp_name_vec.buf_list.bufs[i].len,
                            updated_comp_name_vec.buf_list.bufs[i].data
                        );
                    } else { // relevant install service file exists

                        // add relevant component name into the vector
                        ret = ggl_buf_vec_push(
                            &install_comp_name_buf_vec,
                            updated_comp_name_vec.buf_list.bufs[i]
                        );
                        if (ret != GGL_ERR_OK) {
                            GGL_LOGE("Failed to add the install component name "
                                     "into vector");
                            return;
                        }

                        // run link command
                        static uint8_t link_command_buf[PATH_MAX];
                        GglByteVec link_command_vec
                            = GGL_BYTE_VEC(link_command_buf);
                        ret = ggl_byte_vec_append(
                            &link_command_vec, GGL_STR("sudo systemctl link ")
                        );
                        ggl_byte_vec_chain_append(
                            &ret,
                            &link_command_vec,
                            install_service_file_path_vec.buf
                        );
                        ggl_byte_vec_chain_push(&ret, &link_command_vec, '\0');
                        if (ret != GGL_ERR_OK) {
                            GGL_LOGE("Failed to create systemctl link command."
                            );
                            return;
                        }

                        // NOLINTBEGIN(concurrency-mt-unsafe)
                        int system_ret
                            = system((char *) link_command_vec.buf.data);
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

                        // run start command
                        static uint8_t start_command_buf[PATH_MAX];
                        GglByteVec start_command_vec
                            = GGL_BYTE_VEC(start_command_buf);
                        ret = ggl_byte_vec_append(
                            &start_command_vec, GGL_STR("sudo systemctl start ")
                        );
                        ggl_byte_vec_chain_append(
                            &ret,
                            &start_command_vec,
                            install_service_file_path_vec.buf
                        );
                        ggl_byte_vec_chain_push(&ret, &start_command_vec, '\0');
                        if (ret != GGL_ERR_OK) {
                            GGL_LOGE("Failed to create systemctl start command."
                            );
                            return;
                        }

                        system_ret
                            = system((char *) start_command_vec.buf.data);
                        // NOLINTEND(concurrency-mt-unsafe)
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
                    }
                }
            }

            // wait for all the install status
            ret = wait_for_install_status(install_comp_name_buf_vec);
            if (ret != GGL_ERR_OK) {
                return;
            }

            // process all run or startup files after install only
            for (size_t i = 0; i < updated_comp_name_vec.buf_list.len; i++) {
                static uint8_t service_file_path_buf[PATH_MAX];
                GglByteVec service_file_path_vec
                    = GGL_BYTE_VEC(service_file_path_buf);
                ret = ggl_byte_vec_append(
                    &service_file_path_vec, args->root_path
                );
                ggl_byte_vec_append(&service_file_path_vec, GGL_STR("/"));
                ggl_byte_vec_append(&service_file_path_vec, GGL_STR("ggl."));
                ggl_byte_vec_chain_append(
                    &ret,
                    &service_file_path_vec,
                    updated_comp_name_vec.buf_list.bufs[i]
                );
                ggl_byte_vec_chain_append(
                    &ret, &service_file_path_vec, GGL_STR(".service")
                );
                if (ret == GGL_ERR_OK) {
                    // run link command
                    static uint8_t link_command_buf[PATH_MAX];
                    GglByteVec link_command_vec
                        = GGL_BYTE_VEC(link_command_buf);
                    ret = ggl_byte_vec_append(
                        &link_command_vec, GGL_STR("sudo systemctl link ")
                    );
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
                            GGL_LOGE("sudo systemctl link command failed");
                            return;
                        }
                        GGL_LOGI(
                            "sudo systemctl link exited with child status %d\n",
                            WEXITSTATUS(system_ret)
                        );
                    } else {
                        GGL_LOGE("sudo systemctl link did not exit normally");
                        return;
                    }

                    // run enable command
                    static uint8_t enable_command_buf[PATH_MAX];
                    GglByteVec enable_command_vec
                        = GGL_BYTE_VEC(enable_command_buf);
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
                            GGL_LOGE("sudo systemctl enable failed");
                            return;
                        }
                        GGL_LOGI(
                            "sudo systemctl enable exited with child status "
                            "%d\n",
                            WEXITSTATUS(system_ret)
                        );
                    } else {
                        GGL_LOGE("sudo systemctl enable did not exit normally");
                        return;
                    }
                }
            }

            // run daemon-reload command once all the files are linked
            static uint8_t reload_command_buf[PATH_MAX];
            GglByteVec reload_command_vec = GGL_BYTE_VEC(reload_command_buf);
            ret = ggl_byte_vec_append(
                &reload_command_vec, GGL_STR("sudo systemctl daemon-reload\0")
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to create sudo systemctl "
                         "daemon-reload command.");
                return;
            }

            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            int system_ret = system((char *) reload_command_vec.buf.data);
            if (WIFEXITED(system_ret)) {
                if (WEXITSTATUS(system_ret) != 0) {
                    GGL_LOGE("sudo systemctl daemon-reload failed");
                    return;
                }
                GGL_LOGI(
                    "sudo systemctl daemon-reload exited with child "
                    "status "
                    "%d\n",
                    WEXITSTATUS(system_ret)
                );
            } else {
                GGL_LOGE("sudo systemctl daemon-reload did not exit normally");
                return;
            }
        }

        ret = wait_for_deployment_status(resolved_components_kv_vec.map);
        if (ret != GGL_ERR_OK) {
            return;
        }

        ret = cleanup_stale_versions(resolved_components_kv_vec.map);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Error while cleaning up stale components after deployment."
            );
        }

        ret = send_fss_update(GGL_STR("THING_GROUP_DEPLOYMENT"));
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Error while reporting fleet status after deployment.");
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

            static Recipe2UnitArgs recipe2unit_args;
            memset(&recipe2unit_args, 0, sizeof(Recipe2UnitArgs));
            recipe2unit_args.user = posix_user;
            recipe2unit_args.group = group;

            GGL_LOGI(
                "Recipe path %.*s",
                (int) recipe_path_vec.buf.len,
                recipe_path_vec.buf.data
            );

            recipe2unit_args.component_name = pair->key;
            recipe2unit_args.component_version = pair->val.buf;
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
            recipe2unit_args.root_path_fd = root_path_fd;

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

            ret = ggl_gg_config_write(
                GGL_BUF_LIST(
                    GGL_STR("services"), component_name->buf, GGL_STR("version")
                ),
                pair->val,
                0
            );

            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to write component version to ggconfigd.");
                return;
            }

            ret = add_arn_list_to_config(
                component_name->buf, deployment->configuration_arn
            );

            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to write configuration arn to ggconfigd.");
                return;
            }

            GglObject *intermediate_obj;
            GglObject *default_config_obj;

            if (ggl_map_get(
                    recipe_buff_obj.map,
                    GGL_STR("ComponentConfiguration"),
                    &intermediate_obj
                )) {
                if (intermediate_obj->type != GGL_TYPE_MAP) {
                    GGL_LOGE("ComponentConfiguration is not a map type");
                    return;
                }

                if (ggl_map_get(
                        intermediate_obj->map,
                        GGL_STR("DefaultConfiguration"),
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
        GglError ret = send_fss_update(GGL_STR("LOCAL_DEPLOYMENT"));
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Error while reporting fleet status after deployment.");
        }
    }
    *deployment_succeeded = true;
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

        bool deployment_succeeded = false;
        handle_deployment(deployment, args, &deployment_succeeded);

        // TODO: need error details from handle_deployment
        if (deployment_succeeded) {
            GGL_LOGI("Completed deployment processing and reporting job as "
                     "SUCCEEDED.");
            update_current_jobs_deployment(
                deployment->deployment_id, GGL_STR("SUCCEEDED")
            );
        } else {
            GGL_LOGW(
                "Completed deployment processing and reporting job as FAILED."
            );
            update_current_jobs_deployment(
                deployment->deployment_id, GGL_STR("FAILED")
            );
        }

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
