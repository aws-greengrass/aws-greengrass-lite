// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "deployment_handler.h"
#include "bootstrap_manager.h"
#include "component_config.h"
#include "dependency_resolver.h"
#include "deployment_configuration.h"
#include "deployment_model.h"
#include "deployment_queue.h"
#include "iot_jobs_listener.h"
#include "stale_component.h"
#include <sys/types.h>
#include <assert.h>
#include <fcntl.h>
#include <ggl/base64.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/cleanup.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/core_bus/gg_healthd.h>
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
#include <ggl/process.h>
#include <ggl/recipe.h>
#include <ggl/recipe2unit.h>
#include <ggl/uri.h>
#include <ggl/utils.h>
#include <ggl/vector.h>
#include <ggl/version.h>
#include <ggl/zip.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_DECODE_BUF_LEN 4096

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

static GglError merge_dir_to(GglBuffer source, char *dir) {
    char *mkdir[] = { "mkdir", "-p", dir, NULL };
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

    char *cp[] = { "cp", "-RP", source_path, dir, NULL };
    return ggl_process_call(cp);
}

static GglError get_tes_credentials(TesCredentials *tes_creds) {
    GglObject *aws_access_key_id = NULL;
    GglObject *aws_secret_access_key = NULL;
    GglObject *aws_session_token = NULL;

    static uint8_t credentials_alloc[1500];
    static GglBuffer tesd = GGL_STR("aws_iot_tes");
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
    GglMap linux_manifest = { 0 };
    GglError ret = select_linux_manifest(recipe, &linux_manifest);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GglObject *artifact_list = NULL;
    if (!ggl_map_get(linux_manifest, GGL_STR("Artifacts"), &artifact_list)) {
        return GGL_ERR_PARSE;
    }
    if (artifact_list->type != GGL_TYPE_LIST) {
        return GGL_ERR_PARSE;
    }
    *platform_artifacts = artifact_list->list;
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
    GglBuffer arn_list_mem = GGL_BUF((uint8_t[128 * 10]) { 0 });
    GglBumpAlloc arn_list_balloc = ggl_bump_alloc_init(arn_list_mem);
    GglObject arn_list;

    GglError ret = ggl_gg_config_read(
        GGL_BUF_LIST(GGL_STR("services"), component_name, GGL_STR("configArn")),
        &arn_list_balloc.alloc,
        &arn_list
    );

    if ((ret != GGL_ERR_OK) && (ret != GGL_ERR_NOENTRY)) {
        GGL_LOGE("Failed to retrieve configArn.");
        return GGL_ERR_FAILURE;
    }

    GglObjVec new_arn_list = GGL_OBJ_VEC((GglObject[100]) { 0 });
    if (ret != GGL_ERR_NOENTRY) {
        // list exists in config, parse for current config arn and append if it
        // is not already included
        if (arn_list.type != GGL_TYPE_LIST) {
            GGL_LOGE("Configuration arn list not of expected type.");
            return GGL_ERR_INVALID;
        }
        if (arn_list.list.len >= 100) {
            GGL_LOGE(
                "Cannot append configArn: Component is deployed as part of too "
                "many deployments (%zu >= 100).",
                arn_list.list.len
            );
        }
        GGL_LIST_FOREACH(arn, arn_list.list) {
            if (arn->type != GGL_TYPE_BUF) {
                GGL_LOGE("Configuration arn not of type buffer.");
                return ret;
            }
            if (ggl_buffer_eq(
                    get_unversioned_substring(arn->buf),
                    get_unversioned_substring(configuration_arn)
                )) {
                // arn for this group already added to config, replace it
                GGL_LOGD("Configuration arn already exists for this thing "
                         "group, overwriting it.");
                *arn = GGL_OBJ_BUF(configuration_arn);
                ret = ggl_gg_config_write(
                    GGL_BUF_LIST(
                        GGL_STR("services"),
                        component_name,
                        GGL_STR("configArn")
                    ),
                    GGL_OBJ_LIST(arn_list.list),
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

    ret = ggl_obj_vec_push(&new_arn_list, GGL_OBJ_BUF(configuration_arn));
    assert(ret == GGL_ERR_OK);

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(GGL_STR("services"), component_name, GGL_STR("configArn")),
        GGL_OBJ_LIST(new_arn_list.list),
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
        { GGL_STR("detailedStatus"),
          GGL_OBJ_BUF(
              deployment_succeeded ? GGL_STR("SUCCESSFUL")
                                   : GGL_STR("FAILED_ROLLBACK_NOT_REQUESTED")
          ) },
    );

    GglMap deployment_info = GGL_MAP(
        { GGL_STR("status"),
          GGL_OBJ_BUF(
              deployment_succeeded ? GGL_STR("SUCCEEDED") : GGL_STR("FAILED")
          ) },
        { GGL_STR("fleetConfigurationArnForStatus"),
          GGL_OBJ_BUF(deployment->configuration_arn) },
        { GGL_STR("deploymentId"), GGL_OBJ_BUF(deployment->deployment_id) },
        { GGL_STR("statusDetails"), GGL_OBJ_MAP(status_details_map) },
        { GGL_STR("unchangedRootComponents"), GGL_OBJ_LIST(GGL_LIST()) },
    );

    uint8_t trigger_buffer[24];
    GglBuffer trigger = GGL_BUF(trigger_buffer);

    if (deployment->type == LOCAL_DEPLOYMENT) {
        trigger = GGL_STR("LOCAL_DEPLOYMENT");
    } else if (deployment->type == THING_GROUP_DEPLOYMENT) {
        trigger = GGL_STR("THING_GROUP_DEPLOYMENT");
    }

    GglMap args = GGL_MAP(
        { GGL_STR("trigger"), GGL_OBJ_BUF(trigger) },
        { GGL_STR("deployment_info"), GGL_OBJ_MAP(deployment_info) }
    );

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

static GglError wait_for_phase_status(
    GglBufVec component_vec, GglBuffer phase
) {
    // TODO: hack
    ggl_sleep(5);

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
        ggl_byte_vec_append(&full_comp_name_vec, phase);
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
            GGL_MAP({ GGL_STR("component_name"),
                      GGL_OBJ_BUF(full_comp_name_vec.buf) }),
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
    ggl_sleep(5);

    GGL_MAP_FOREACH(component, resolved_components) {
        GGL_LOGD(
            "Waiting for %.*s to finish",
            (int) component->key.len,
            component->key.data
        );
        GglError ret = ggl_sub_response(
            GGL_STR("gg_health"),
            GGL_STR("subscribe_to_lifecycle_completion"),
            GGL_MAP({ GGL_STR("component_name"), GGL_OBJ_BUF(component->key) }),
            deployment_status_callback,
            NULL,
            NULL,
            300
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed waiting for %.*s",
                (int) component->key.len,
                component->key.data
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
    GglBumpAlloc resolve_dependencies_balloc
        = ggl_bump_alloc_init(GGL_BUF(resolve_dependencies_mem));
    GglError ret = resolve_dependencies(
        deployment->components,
        deployment->thing_group,
        args,
        &resolve_dependencies_balloc.alloc,
        &resolved_components_kv_vec
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to do dependency resolution for deployment, failing "
                 "deployment.");
        return;
    }

    GglByteVec region = GGL_BYTE_VEC(config.region);
    ret = read_nucleus_config(GGL_STR("awsRegion"), &region.buf);
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

    // list of {component name -> component version} for all new components in
    // the deployment
    GglKVVec components_to_deploy = GGL_KV_VEC((GglKV[64]) { 0 });

    GGL_MAP_FOREACH(pair, resolved_components_kv_vec.map) {
        // check config to see if component has completed processing
        uint8_t resp_mem[128] = { 0 };
        GglBuffer resp = GGL_BUF(resp_mem);

        ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("DeploymentService"),
                GGL_STR("deploymentState"),
                GGL_STR("components"),
                pair->key
            ),
            &resp
        );
        if (ret == GGL_ERR_OK) {
            GGL_LOGD(
                "Component %.*s completed processing in previous run. Will not "
                "be reprocessed.",
                (int) pair->key.len,
                pair->key.data
            );
            continue;
        }

        // check config to see if bootstrap steps have already been run for this
        // component
        if (component_bootstrap_phase_completed(pair->key)) {
            GGL_LOGD(
                "Bootstrap component %.*s encountered. Bootstrap phase has "
                "already been completed. Adding to list of components to "
                "process to complete any other lifecycle stages.",
                (int) pair->key.len,
                pair->key.data
            );
            ret = ggl_kv_vec_push(
                &components_to_deploy, (GglKV) { pair->key, pair->val }
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "Failed to add component info for %.*s to deployment "
                    "vector.",
                    (int) pair->key.len,
                    pair->key.data
                );
                return;
            }
            continue;
        }

        int component_artifacts_fd = -1;
        ret = open_component_artifacts_dir(
            artifact_store_fd, pair->key, pair->val.buf, &component_artifacts_fd
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to open artifact directory.");
            return;
        }
        int component_archive_dir_fd = -1;
        ret = open_component_artifacts_dir(
            artifact_archive_fd,
            pair->key,
            pair->val.buf,
            &component_archive_dir_fd
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to open unarchived artifacts directory.");
            return;
        }
        GglObject recipe_obj;
        static uint8_t recipe_mem[MAX_RECIPE_MEM] = { 0 };
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

        uint8_t thing_name_arr[128];
        GglBuffer thing_name = GGL_BUF(thing_name_arr);
        ret = read_system_config(GGL_STR("thingName"), &thing_name);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to get thing name.");
            return;
        }

        uint8_t root_ca_path_arr[PATH_MAX];
        GglBuffer root_ca_path = GGL_BUF(root_ca_path_arr);
        ret = read_system_config(GGL_STR("rootCaPath"), &root_ca_path);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to get rootCaPath.");
            return;
        }

        uint8_t posix_user_arr[128];
        GglBuffer posix_user = GGL_BUF(posix_user_arr);
        ret = get_posix_user(&posix_user);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to get posix_user.");
            return;
        }
        if (posix_user.len < 1) {
            GGL_LOGE("Run with default posix user is not set.");
            return;
        }
        bool colon_found = false;
        char *group;
        for (size_t j = 0; j < posix_user.len; j++) {
            if (posix_user.data[j] == ':') {
                posix_user.data[j] = '\0';
                colon_found = true;
                group = (char *) &posix_user.data[j + 1];
                break;
            }
        }
        if (!colon_found) {
            group = (char *) posix_user.data;
        }

        static Recipe2UnitArgs recipe2unit_args;
        memset(&recipe2unit_args, 0, sizeof(Recipe2UnitArgs));
        recipe2unit_args.user = (char *) posix_user.data;
        recipe2unit_args.group = group;

        recipe2unit_args.component_name = pair->key;
        recipe2unit_args.component_version = pair->val.buf;

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
        static uint8_t big_buffer_for_bump[MAX_RECIPE_MEM];
        GglBumpAlloc bump_alloc
            = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));
        HasPhase phases = { 0 };
        GglError err = convert_to_unit(
            &recipe2unit_args,
            &bump_alloc.alloc,
            &recipe_buff_obj,
            &component_name,
            &phases
        );

        if (err != GGL_ERR_OK) {
            return;
        }

        if (!ggl_buffer_eq(component_name->buf, pair->key)) {
            GGL_LOGE("Component name from recipe does not match component name "
                     "from recipe file.");
            return;
        }

        // TODO: See if there is a better requirement. If a customer has the
        // same version as before but somehow updated their component
        // version their component may not get the updates.
        bool component_updated = true;

        static uint8_t old_component_version_mem[128] = { 0 };
        GglBuffer old_component_version = GGL_BUF(old_component_version_mem);
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

        ret = ggl_gg_config_write(
            GGL_BUF_LIST(
                GGL_STR("services"), component_name->buf, GGL_STR("version")
            ),
            pair->val,
            &(int64_t) { 0 }
        );

        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to write version of %.*s to ggconfigd.",
                (int) pair->key.len,
                pair->key.data
            );
            return;
        }

        ret = add_arn_list_to_config(
            component_name->buf, deployment->configuration_arn
        );

        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to write configuration arn of %.*s to ggconfigd.",
                (int) pair->key.len,
                pair->key.data
            );
            return;
        }

        ret = apply_configurations(
            deployment, component_name->buf, GGL_STR("reset")
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to apply reset configuration update for %.*s.",
                (int) pair->key.len,
                pair->key.data
            );
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
                    &(int64_t) { 0 }
                );

                if (ret != GGL_ERR_OK) {
                    GGL_LOGE("Failed to send default config to ggconfigd.");
                    return;
                }
            } else {
                GGL_LOGI(
                    "DefaultConfiguration not found in the recipe of %.*s.",
                    (int) pair->key.len,
                    pair->key.data
                );
            }
        } else {
            GGL_LOGI(
                "ComponentConfiguration not found in the recipe of %.*s.",
                (int) pair->key.len,
                pair->key.data
            );
        }

        ret = apply_configurations(
            deployment, component_name->buf, GGL_STR("merge")
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to apply merge configuration update for %.*s.",
                (int) pair->key.len,
                pair->key.data
            );
            return;
        }

        if (component_updated) {
            ret = ggl_kv_vec_push(
                &components_to_deploy, (GglKV) { pair->key, pair->val }
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "Failed to add component info for %.*s to deployment "
                    "vector.",
                    (int) pair->key.len,
                    pair->key.data
                );
                return;
            }
            GGL_LOGD(
                "Added %.*s to list of components that need to be processed.",
                (int) pair->key.len,
                pair->key.data
            );
        } else {
            // component already exists, check its lifecycle state
            uint8_t component_status_arr[NAME_MAX];
            GglBuffer component_status = GGL_BUF(component_status_arr);
            ret = ggl_gghealthd_retrieve_component_status(
                pair->key, &component_status
            );

            if (ret != GGL_ERR_OK) {
                GGL_LOGD(
                    "Failed to retrieve health status for %.*s. Redeploying "
                    "component.",
                    (int) pair->key.len,
                    pair->key.data
                );
                ret = ggl_kv_vec_push(
                    &components_to_deploy, (GglKV) { pair->key, pair->val }
                );
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE(
                        "Failed to add component info for %.*s to deployment "
                        "vector.",
                        (int) pair->key.len,
                        pair->key.data
                    );
                    return;
                }
                GGL_LOGD(
                    "Added %.*s to list of components that need to be "
                    "processed.",
                    (int) pair->key.len,
                    pair->key.data
                );
            }

            // Skip redeploying components in a RUNNING state
            if (ggl_buffer_eq(component_status, GGL_STR("RUNNING"))
                || ggl_buffer_eq(component_status, GGL_STR("FINISHED"))) {
                GGL_LOGD(
                    "Component %.*s is already running. Will not redeploy.",
                    (int) pair->key.len,
                    pair->key.data
                );
                // save as a deployed component in case of bootstrap
                ret = save_component_info(
                    pair->key, pair->val.buf, GGL_STR("completed")
                );
                if (ret != GGL_ERR_OK) {
                    return;
                }
            } else {
                ret = ggl_kv_vec_push(
                    &components_to_deploy, (GglKV) { pair->key, pair->val }
                );
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE(
                        "Failed to add component info for %.*s to deployment "
                        "vector.",
                        (int) pair->key.len,
                        pair->key.data
                    );
                    return;
                }
                GGL_LOGD(
                    "Added %.*s to list of components that need to be "
                    "processed.",
                    (int) pair->key.len,
                    pair->key.data
                );
            }
        }
    }

    // TODO:: Add a logic to only run the phases that exist with the latest
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
        GGL_MAP_FOREACH(component, components_to_deploy.map) {
            GglBuffer component_name = component->key;

            static uint8_t install_service_file_path_buf[PATH_MAX];
            GglByteVec install_service_file_path_vec
                = GGL_BYTE_VEC(install_service_file_path_buf);
            ret = ggl_byte_vec_append(
                &install_service_file_path_vec, args->root_path
            );
            ggl_byte_vec_append(&install_service_file_path_vec, GGL_STR("/"));
            ggl_byte_vec_append(
                &install_service_file_path_vec, GGL_STR("ggl.")
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
                    disable_and_unlink_service(&component_name, INSTALL);
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
        GGL_MAP_FOREACH(component, components_to_deploy.map) {
            GglBuffer component_name = component->key;
            GglBuffer component_version = component->val.buf;

            static uint8_t service_file_path_buf[PATH_MAX];
            GglByteVec service_file_path_vec
                = GGL_BYTE_VEC(service_file_path_buf);
            ret = ggl_byte_vec_append(&service_file_path_vec, args->root_path);
            ggl_byte_vec_append(&service_file_path_vec, GGL_STR("/"));
            ggl_byte_vec_append(&service_file_path_vec, GGL_STR("ggl."));
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
                    disable_and_unlink_service(&component_name, RUN_STARTUP);
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
        update_bootstrap_jobs_deployment(
            bootstrap_deployment.deployment_id,
            GGL_STR("IN_PROGRESS"),
            jobs_version
        );

        bool bootstrap_deployment_succeeded = false;
        handle_deployment(
            &bootstrap_deployment, args, &bootstrap_deployment_succeeded
        );

        send_fss_update(&bootstrap_deployment, bootstrap_deployment_succeeded);

        if (bootstrap_deployment_succeeded) {
            GGL_LOGI("Completed deployment processing and reporting job as "
                     "SUCCEEDED.");
            update_bootstrap_jobs_deployment(
                bootstrap_deployment.deployment_id,
                GGL_STR("SUCCEEDED"),
                jobs_version
            );
        } else {
            GGL_LOGW("Completed deployment processing and reporting job as "
                     "FAILED.");
            update_bootstrap_jobs_deployment(
                bootstrap_deployment.deployment_id,
                GGL_STR("FAILED"),
                jobs_version
            );
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

        update_current_jobs_deployment(
            deployment->deployment_id, GGL_STR("IN_PROGRESS")
        );

        bool deployment_succeeded = false;
        handle_deployment(deployment, args, &deployment_succeeded);

        send_fss_update(deployment, deployment_succeeded);

        // TODO: need error details from handle_deployment
        if (deployment_succeeded) {
            GGL_LOGI("Completed deployment processing and reporting job as "
                     "SUCCEEDED.");
            update_current_jobs_deployment(
                deployment->deployment_id, GGL_STR("SUCCEEDED")
            );
        } else {
            GGL_LOGW("Completed deployment processing and reporting job as "
                     "FAILED.");
            update_current_jobs_deployment(
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
