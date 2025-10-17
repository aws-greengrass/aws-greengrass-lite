// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "cloud_request.h"
#include "config_operations.h"
#include "fleet-provisioning.h"
#include "ggl/cleanup.h"
#include "ggl/exec.h"
#include "pki_ops.h"
#include "stdbool.h"
#include <fcntl.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/json_decode.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/utils.h>
#include <ggl/vector.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_TEMPLATE_LEN 128
#define MAX_ENDPOINT_LENGTH 128
#define MAX_TEMPLATE_PARAM_LEN 4096
#define MAX_CSR_LENGTH 4096

#define USER_GROUP (GGL_SYSTEMD_SYSTEM_USER ":" GGL_SYSTEMD_SYSTEM_GROUP)

static GglError cleanup_actions(
    GglBuffer output_dir_path,
    GglBuffer tmp_cert_path,
    GglBuffer thing_name,
    FleetProvArgs *args
) {
    // Create destination directory
    const char *mkdir_dest_args[]
        = { "mkdir", "-p", (char *) output_dir_path.data, NULL };
    GglError ret = ggl_exec_command(mkdir_dest_args);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to create destination directory");
        return ret;
    }
    GGL_LOGI("Successfully created destination directory");

    // Copy certificates from output_dir contents to destination_dir (overwrite
    // existing)
    static uint8_t cmd_mem[PATH_MAX * 2];
    GglByteVec cmd = GGL_BYTE_VEC(cmd_mem);
    ret = ggl_byte_vec_append(&cmd, GGL_STR("cp -rf "));
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    ret = ggl_byte_vec_append(&cmd, tmp_cert_path);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    ret = ggl_byte_vec_append(&cmd, GGL_STR("* "));
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    ret = ggl_byte_vec_append(&cmd, output_dir_path);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    ret = ggl_byte_vec_push(&cmd, '\0');
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    const char *sh_args[] = { "sh", "-c", (char *) cmd.buf.data, NULL };
    ret = ggl_exec_command(sh_args);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to copy certificates to destination directory");
        return ret;
    }
    GGL_LOGI("Successfully copied certificates to destination directory");

    ret = ggl_update_system_cert_paths(output_dir_path, args, thing_name);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_update_iot_endpoints(args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    const char *chown_args[]
        = { "chown", "-R", USER_GROUP, (char *) output_dir_path.data, NULL };

    ret = ggl_exec_command(chown_args);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to change ownership of certificates");
        return ret;
    }
    GGL_LOGI(
        "Successfully changed ownership of certificates to %s", USER_GROUP
    );

    return GGL_ERR_OK;
}

static GglError start_iotcored(FleetProvArgs *args, pid_t *iotcored_pid) {
    static uint8_t uuid_mem[37];
    uuid_t binuuid;
    uuid_generate_random(binuuid);
    uuid_unparse(binuuid, (char *) uuid_mem);
    uuid_mem[36] = '\0';

    const char *iotcore_d_args[]
        = { args->iotcored_path, "-n", "iotcoredfleet",   "-e",
            args->endpoint,      "-i", (char *) uuid_mem, "-r",
            args->root_ca_path,  "-c", args->claim_cert,  "-k",
            args->claim_key,     NULL };

    GglError ret = ggl_exec_command_async(iotcore_d_args, iotcored_pid);

    GGL_LOGD("PID for new iotcored: %d", *iotcored_pid);

    return ret;
}

static void cleanup_kill_process(const pid_t *pid) {
    (void) ggl_exec_kill_process(*pid);
}

GglError run_fleet_prov(FleetProvArgs *args) {
    uint8_t config_resp_mem[PATH_MAX] = { 0 };
    GglArena alloc = ggl_arena_init(GGL_BUF(config_resp_mem));

    bool enabled = false;
    GglError ret = ggl_has_provisioning_config(alloc, &enabled);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    if (!enabled) {
        return GGL_ERR_OK;
    }

    // Skip if already provisioned
    bool provisioned = false;
    ret = ggl_is_already_provisioned(alloc, &provisioned);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    if (provisioned) {
        GGL_LOGI("Skipping provisioning.");
        return GGL_ERR_OK;
    }

    GglBuffer tmp_cert_path = GGL_STR("/tmp/provisioning/");

    ret = ggl_get_configuration(args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    int output_dir;
    ret = ggl_dir_open(tmp_cert_path, O_PATH, true, &output_dir);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Error opening output directory %.*s.",
            (int) tmp_cert_path.len,
            tmp_cert_path.data
        );
        return ret;
    }
    GGL_CLEANUP(cleanup_close, output_dir);

    pid_t iotcored_pid = -1;
    ret = start_iotcored(args, &iotcored_pid);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_CLEANUP(cleanup_kill_process, iotcored_pid);

    int priv_key;
    ret = ggl_file_openat(
        output_dir, GGL_STR("priv_key"), O_RDWR | O_CREAT, 0600, &priv_key
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error opening private key file for writing.");
        return ret;
    }
    GGL_CLEANUP(cleanup_close, priv_key);

    int pub_key;
    ret = ggl_file_openat(
        output_dir, GGL_STR("pub_key.pub"), O_RDWR | O_CREAT, 0600, &pub_key
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error opening public key file for writing.");
        return ret;
    }
    GGL_CLEANUP(cleanup_close, pub_key);

    int cert_req;
    ret = ggl_file_openat(
        output_dir, GGL_STR("cert_req.pem"), O_RDWR | O_CREAT, 0600, &cert_req
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error opening CSR file for writing.");
        return ret;
    }
    GGL_CLEANUP(cleanup_close, cert_req);

    ret = ggl_pki_generate_keypair(priv_key, pub_key, cert_req);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    (void) lseek(priv_key, 0, SEEK_SET);
    (void) lseek(pub_key, 0, SEEK_SET);
    (void) lseek(cert_req, 0, SEEK_SET);

    // Read CSR from file descriptor
    uint8_t csr_mem[MAX_CSR_LENGTH] = { 0 };
    ssize_t csr_len = read(cert_req, csr_mem, sizeof(csr_mem) - 1);
    if (csr_len <= 0) {
        GGL_LOGE("Failed to read CSR from file.");
        return GGL_ERR_FAILURE;
    }
    GglBuffer csr_buf = { .data = csr_mem, .len = (size_t) csr_len };

    // Parse template parameters
    GglObject template_params_obj;
    ret = ggl_json_decode_destructive(
        ggl_buffer_from_null_term(args->template_params),
        &alloc,
        &template_params_obj
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to parse template parameters.");
        return ret;
    }
    if (ggl_obj_type(template_params_obj) != GGL_TYPE_MAP) {
        GGL_LOGE("Template parameters must be a JSON object.");
        return GGL_ERR_INVALID;
    }

    // Create certificate output file
    int certificate_fd;
    ret = ggl_file_openat(
        output_dir,
        GGL_STR("certificate.pem"),
        O_RDWR | O_CREAT,
        0600,
        &certificate_fd
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error opening certificate file for writing.");
        return ret;
    }
    GGL_CLEANUP(cleanup_close, certificate_fd);

    // Wait for MQTT(iotcored) connection to establish
    (void) ggl_sleep(5);

    static uint8_t thing_name_mem[128];
    GglBuffer thing_name = GGL_BUF(thing_name_mem);

    ret = ggl_get_certificate_from_aws(
        csr_buf,
        ggl_buffer_from_null_term(args->template_name),
        ggl_obj_into_map(template_params_obj),
        &thing_name,
        certificate_fd
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglBuffer output_dir_path = GGL_STR("/var/lib/greengrass/credentials/");
    if (args->output_dir != NULL) {
        output_dir_path = ggl_buffer_from_null_term(args->output_dir);
    }

    ret = cleanup_actions(output_dir_path, tmp_cert_path, thing_name, args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}
