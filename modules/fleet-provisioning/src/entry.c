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
#include <gg/types.h>
#include <gg/utils.h>
#include <ggl/exec.h>
#include <limits.h>
#include <string.h>
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

static GgError change_custom_file_ownership(FleetProvArgs *args) {
    GgError ret;
    if (args->cert_path != NULL) {
        const char *cert_chown_args[]
            = { "chown", USER_GROUP, args->cert_path, NULL };
        ret = ggl_exec_command(cert_chown_args);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to change ownership of custom certificate");
            return ret;
        }
    }
    if (args->key_path != NULL && strncmp(args->claim_key, "handle:", 7) != 0) {
        const char *key_chown_args[]
            = { "chown", USER_GROUP, args->key_path, NULL };
        ret = ggl_exec_command(key_chown_args);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to change ownership of custom private key");
            return ret;
        }
    }
    if (args->csr_path != NULL) {
        if (unlink(args->csr_path) != 0) {
            GG_LOGW("Failed to remove CSR file");
        }
    }
    return GG_ERR_OK;
}

static GgError cleanup_actions(
    GgBuffer output_dir_path, GgBuffer thing_name, FleetProvArgs *args
) {
    GgError ret = ggl_update_system_config(output_dir_path, args, thing_name);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = ggl_update_iot_endpoints(args);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = change_custom_file_ownership(args);
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

    // Update system certificate file path
    ret = ggl_update_system_cert_path(output_dir_path, args);
    if (ret != GG_ERR_OK) {
        return ret;
    }

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

static GgError open_file_or_default(
    char *custom_path,
    int output_dir,
    GgBuffer default_name,
    const char *error_msg,
    int *fd
) {
    GgError ret;
    if (custom_path != NULL) {
        if (default_name.len == 0) {
            ret = gg_file_open(
                gg_buffer_from_null_term(custom_path), O_RDONLY, 0, fd
            );
        } else {
            ret = gg_file_open(
                gg_buffer_from_null_term(custom_path),
                O_RDWR | O_CREAT | O_TRUNC,
                0600,
                fd
            );
        }

    } else {
        ret = gg_file_openat(
            output_dir, default_name, O_RDWR | O_CREAT | O_TRUNC, 0600, fd
        );
    }
    if (ret != GG_ERR_OK) {
        GG_LOGE("%s", error_msg);
    }
    return ret;
}

static GgError handle_pki_flow(
    FleetProvArgs *args, int output_dir, int *csr_fd_out
) {
    GgError ret;
    int priv_key = -1;

    // Open or create private key file
    ret = open_file_or_default(
        args->key_path,
        output_dir,
        GG_STR("priv_key"),
        "Error opening private key file.",
        &priv_key
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }
    GG_CLEANUP(cleanup_close, priv_key);

    // Open or create CSR file
    ret = open_file_or_default(
        args->csr_path,
        output_dir,
        GG_STR("cert_req.pem"),
        "Error opening CSR file.",
        csr_fd_out
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // Generate keypair + CSR using PKI
    ret = ggl_pki_generate_keypair(
        priv_key, *csr_fd_out, args->csr_common_name
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("PKI: Failed to generate keypair + CSR");
        return ret;
    }

    // Reset position for future reads
    (void) lseek(priv_key, 0, SEEK_SET);

    return GG_ERR_OK;
}

static GgError handle_tpm_flow(
    FleetProvArgs *args,
    int output_dir,
    int *csr_fd_out,
    const char *handle_path
) {
    GgError ret;

    // Open or create CSR file
    ret = open_file_or_default(
        args->csr_path,
        output_dir,
        GG_STR("cert_req.pem"),
        "Error opening CSR file.",
        csr_fd_out
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // Generate a CSR using TPM persistent handle
    ret = ggl_tpm_pki_generate_csr(
        *csr_fd_out, args->csr_common_name, handle_path
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to generate a CSR from a TPM persistent handle.");
        return ret;
    }

    return GG_ERR_OK;
}

GgError run_fleet_prov(FleetProvArgs *args) {
    uint8_t config_resp_mem[PATH_MAX] = { 0 };
    GgArena alloc = gg_arena_init(GG_BUF(config_resp_mem));

    static uint8_t template_params_mem[MAX_TEMPLATE_PARAM_LEN] = { 0 };
    GgArena template_alloc = gg_arena_init(GG_BUF(template_params_mem));
    GgMap template_params = { 0 };

    GgError ret;

    // Config checks
    bool enabled = false;
    ret = ggl_has_provisioning_config(alloc, &enabled);
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

    ret = ggl_get_configuration(args);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = ggl_load_template_params(args, &template_alloc, &template_params);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // Output dir
    GgBuffer output_dir_path = args->output_dir
        ? gg_buffer_from_null_term(args->output_dir)
        : GG_STR("/var/lib/greengrass/credentials");

    int output_dir;
    ret = gg_dir_open(output_dir_path, O_PATH, true, &output_dir);
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Error opening output directory %.*s.",
            (int) output_dir_path.len,
            output_dir_path.data
        );
        return ret;
    }
    GG_CLEANUP(cleanup_close, output_dir);

    // Start IoTCored
    pid_t pid = -1;
    ret = start_iotcored(args, &pid);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    GG_CLEANUP(cleanup_kill_process, pid);

    // TPM or regular pki
    int cert_req = -1;
    const char *handle_path = args->key_path ? args->key_path : args->claim_key;
    if (handle_path && strncmp(handle_path, "handle:", 7) == 0) {
        ret = handle_tpm_flow(args, output_dir, &cert_req, handle_path);
    } else {
        ret = handle_pki_flow(args, output_dir, &cert_req);
    }
    if (ret != GG_ERR_OK) {
        return ret;
    }
    GG_CLEANUP(cleanup_close, cert_req);
    (void) lseek(cert_req, 0, SEEK_SET);

    // Read CSR
    uint8_t csr_mem[MAX_CSR_LENGTH] = { 0 };
    ssize_t csr_len = read(cert_req, csr_mem, sizeof(csr_mem) - 1);
    if (csr_len <= 0) {
        GG_LOGE("Failed to read CSR from file.");
        return GG_ERR_FAILURE;
    }
    GgBuffer csr_buf = { csr_mem, (size_t) csr_len };

    // Create certificate output file
    int cert_fd;
    ret = open_file_or_default(
        args->cert_path,
        output_dir,
        GG_STR("certificate.pem"),
        "Error opening certificate file.",
        &cert_fd
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }
    GG_CLEANUP(cleanup_close, cert_fd);

    // Wait for MQTT(iotcored) connection to establish
    (void) gg_sleep(5);

    static uint8_t thing_name_mem[128];
    GgBuffer thing_name = GG_BUF(thing_name_mem);

    ret = ggl_get_certificate_from_aws(
        csr_buf,
        gg_buffer_from_null_term(args->template_name),
        template_params,
        &thing_name,
        cert_fd
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = cleanup_actions(output_dir_path, thing_name, args);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GG_LOGI("Process Complete, Your device is now provisioned");
    return GG_ERR_OK;
}
