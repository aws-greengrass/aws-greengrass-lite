// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "cloud_request.h"
#include "config_operations.h"
#include "pki_ops.h"
#include <fcntl.h>
#include <fleet-provisioning.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/object.h>
#include <gg/utils.h>
#include <gg/vector.h>
#include <ggl/exec.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_TEMPLATE_LEN 128
#define MAX_ENDPOINT_LENGTH 128
#define MAX_TEMPLATE_PARAM_LEN 4096
#define MAX_CSR_LENGTH 4096

#define USER_GROUP (GGL_SYSTEMD_SYSTEM_USER ":" GGL_SYSTEMD_SYSTEM_GROUP)

static GgError cleanup_actions(
    GgBuffer output_dir_path,
    GgBuffer tmp_cert_path,
    GgBuffer thing_name,
    FleetProvArgs *args
) {
    // Create destination directory
    const char *mkdir_dest_args[]
        = { "mkdir", "-p", (char *) output_dir_path.data, NULL };
    GgError ret = ggl_exec_command(mkdir_dest_args);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to create destination directory");
        return ret;
    }
    GG_LOGI("Successfully created destination directory");

    // Copy certificates from output_dir contents to destination_dir (overwrite
    // existing)
    static uint8_t cmd_mem[PATH_MAX * 2];
    GgByteVec cmd = GG_BYTE_VEC(cmd_mem);
    ret = gg_byte_vec_append(&cmd, GG_STR("cp -rf "));
    if (ret != GG_ERR_OK) {
        return ret;
    }
    ret = gg_byte_vec_append(&cmd, tmp_cert_path);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    ret = gg_byte_vec_append(&cmd, GG_STR("* "));
    if (ret != GG_ERR_OK) {
        return ret;
    }
    ret = gg_byte_vec_append(&cmd, output_dir_path);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    ret = gg_byte_vec_push(&cmd, '\0');
    if (ret != GG_ERR_OK) {
        return ret;
    }

    const char *sh_args[] = { "sh", "-c", (char *) cmd.buf.data, NULL };
    ret = ggl_exec_command(sh_args);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to copy certificates to destination directory");
        return ret;
    }
    GG_LOGI("Successfully copied certificates to destination directory");

    ret = ggl_update_system_cert_paths(output_dir_path, args, thing_name);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = ggl_update_iot_endpoints(args);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    const char *chown_args[]
        = { "chown", "-R", USER_GROUP, (char *) output_dir_path.data, NULL };

    ret = ggl_exec_command(chown_args);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to change ownership of certificates");
        return ret;
    }
    GG_LOGI("Successfully changed ownership of certificates to %s", USER_GROUP);

    return GG_ERR_OK;
}

static GgError start_iotcored(FleetProvArgs *args, pid_t *iotcored_pid) {
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

    GgError ret = ggl_exec_command_async(iotcore_d_args, iotcored_pid);

    GG_LOGD("PID for new iotcored: %d", *iotcored_pid);

    return ret;
}

static void cleanup_kill_process(const pid_t *pid) {
    (void) ggl_exec_kill_process(*pid);
}

GgError run_fleet_prov(FleetProvArgs *args) {
    uint8_t config_resp_mem[PATH_MAX] = { 0 };
    GgArena alloc = gg_arena_init(GG_BUF(config_resp_mem));

    static uint8_t template_params_mem[MAX_TEMPLATE_PARAM_LEN] = { 0 };
    GgArena template_alloc = gg_arena_init(GG_BUF(template_params_mem));
    GgMap template_params = { 0 };

    bool enabled = false;
    GgError ret = ggl_has_provisioning_config(alloc, &enabled);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    if (!enabled) {
        return GG_ERR_OK;
    }

    // Skip if already provisioned
    bool provisioned = false;
    ret = ggl_is_already_provisioned(alloc, &provisioned);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    if (provisioned) {
        GG_LOGI("Skipping provisioning.");
        return GG_ERR_OK;
    }

    GgBuffer tmp_cert_path = GG_STR("/tmp/provisioning/");

    ret = ggl_get_configuration(args);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = ggl_load_template_params(args, &template_alloc, &template_params);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    int output_dir;
    ret = gg_dir_open(tmp_cert_path, O_PATH, true, &output_dir);
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Error opening output directory %.*s.",
            (int) tmp_cert_path.len,
            tmp_cert_path.data
        );
        return ret;
    }
    GG_CLEANUP(cleanup_close, output_dir);

    pid_t iotcored_pid = -1;
    ret = start_iotcored(args, &iotcored_pid);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    GG_CLEANUP(cleanup_kill_process, iotcored_pid);

    int priv_key;
    ret = gg_file_openat(
        output_dir,
        GG_STR("priv_key"),
        O_RDWR | O_CREAT | O_TRUNC,
        0600,
        &priv_key
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Error opening private key file for writing.");
        return ret;
    }
    GG_CLEANUP(cleanup_close, priv_key);

    int pub_key;
    ret = gg_file_openat(
        output_dir,
        GG_STR("pub_key.pub"),
        O_RDWR | O_CREAT | O_TRUNC,
        0600,
        &pub_key
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Error opening public key file for writing.");
        return ret;
    }
    GG_CLEANUP(cleanup_close, pub_key);

    int cert_req;
    ret = gg_file_openat(
        output_dir,
        GG_STR("cert_req.pem"),
        O_RDWR | O_CREAT | O_TRUNC,
        0600,
        &cert_req
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Error opening CSR file for writing.");
        return ret;
    }
    GG_CLEANUP(cleanup_close, cert_req);

    ret = ggl_pki_generate_keypair(
        priv_key, pub_key, cert_req, args->csr_common_name
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    (void) lseek(priv_key, 0, SEEK_SET);
    (void) lseek(pub_key, 0, SEEK_SET);
    (void) lseek(cert_req, 0, SEEK_SET);

    // Read CSR from file descriptor
    uint8_t csr_mem[MAX_CSR_LENGTH] = { 0 };
    ssize_t csr_len = read(cert_req, csr_mem, sizeof(csr_mem) - 1);
    if (csr_len <= 0) {
        GG_LOGE("Failed to read CSR from file.");
        return GG_ERR_FAILURE;
    }
    GgBuffer csr_buf = { .data = csr_mem, .len = (size_t) csr_len };

    // Create certificate output file
    int certificate_fd;
    ret = gg_file_openat(
        output_dir,
        GG_STR("certificate.pem"),
        O_RDWR | O_CREAT | O_TRUNC,
        0600,
        &certificate_fd
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Error opening certificate file for writing.");
        return ret;
    }
    GG_CLEANUP(cleanup_close, certificate_fd);

    // Wait for MQTT(iotcored) connection to establish
    (void) gg_sleep(5);

    static uint8_t thing_name_mem[128];
    GgBuffer thing_name = GG_BUF(thing_name_mem);

    ret = ggl_get_certificate_from_aws(
        csr_buf,
        gg_buffer_from_null_term(args->template_name),
        template_params,
        &thing_name,
        certificate_fd
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GgBuffer output_dir_path = GG_STR("/var/lib/greengrass/credentials/");
    if (args->output_dir != NULL) {
        output_dir_path = gg_buffer_from_null_term(args->output_dir);
    }

    ret = cleanup_actions(output_dir_path, tmp_cert_path, thing_name, args);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GG_LOGI("Process Complete, Your device is now provisioned");
    return GG_ERR_OK;
}
