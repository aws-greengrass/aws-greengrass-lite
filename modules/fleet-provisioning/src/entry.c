// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "fleet-provisioning.h"
#include "generate_certificate.h"
#include "ggl/exec.h"
#include "provisioner.h"
#include "stdbool.h"
#include <fcntl.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <limits.h>
#include <openssl/evp.h>
#include <openssl/types.h>
#include <openssl/x509.h>
#include <string.h>
#include <sys/types.h>
#include <uuid/uuid.h>
#include <stdint.h>

#define MAX_TEMPLATE_LEN 129
#define MAX_ENDPOINT_LENGTH 129
#define MAX_TEMPLATE_PARAM_LEN 4096
#define MAX_PATH_LEN 4096

#define USER_GROUP (GGL_SYSTEMD_SYSTEM_USER ":" GGL_SYSTEMD_SYSTEM_GROUP)

GglBuffer ggcredentials_path = GGL_STR("/var/lib/greengrass/provisioned-cert/");

static GglError start_iotcored(FleetProvArgs *args, pid_t *iotcored_pid) {
    static uint8_t uuid_mem[37];
    uuid_t binuuid;
    uuid_generate_random(binuuid);
    uuid_unparse(binuuid, (char *) uuid_mem);
    uuid_mem[36] = '\0';

    const char *iotcore_d_args[]
        = { args->iotcored_path,  "-n", "iotcoredfleet",       "-e",
            args->data_endpoint,  "-i", (char *) uuid_mem,     "-r",
            args->root_ca_path,   "-c", args->claim_cert_path, "-k",
            args->claim_key_path, NULL };

    GglError ret = ggl_exec_command_async(iotcore_d_args, iotcored_pid);

    GGL_LOGD("PID for new iotcored: %d", *iotcored_pid);

    return ret;
}

static GglError check_if_claim_cert(FleetProvArgs args) {
    if (args.claim_key_path == NULL) {
        GGL_LOGD("Checking value with db for "
                 "services/aws.greengrass.fleet_provisioning/configuration/"
                 "claimKeyPath");
        uint8_t claim_key_path_mem[PATH_MAX] = { 0 };
        GglArena alloc = ggl_arena_init(ggl_buffer_substr(
            GGL_BUF(claim_key_path_mem), 0, sizeof(claim_key_path_mem) - 1
        ));
        GglBuffer claim_key_path;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("claimKeyPath")
            ),
            &alloc,
            &claim_key_path
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    GGL_LOGI("Config value for "
             "services/aws.greengrass.fleet_provisioning/claimKeyPath is set.");
    return GGL_ERR_OK;
}

static GglError fetch_prov_status(void) {
    GGL_LOGD("Requesting db for system/privateKeyPath");
    uint8_t mem[MAX_PATH_LEN] = { 0 };
    GglArena alloc = ggl_arena_init(GGL_BUF(mem));
    GglBuffer private_key = { 0 };
    GglError ret_private_key = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("privateKeyPath")),
        &alloc,
        &private_key
    );
    if (ret_private_key != GGL_ERR_OK) {
        // If the key is not found then we still want to provision the device
        if (ret_private_key == GGL_ERR_NOENTRY) {
            return GGL_ERR_OK;
        }
        return ret_private_key;
    }
    if (private_key.len != 0) {
        GGL_LOGI("Config value for system/privateKeyPath is set.");
        return GGL_ERR_EXPECTED;
    }

    return GGL_ERR_OK;
}

static GglError fetch_from_db(FleetProvArgs *args) {
    if (args->claim_cert_path == NULL) {
        GGL_LOGD("Requesting db for "
                 "services/aws.greengrass.fleet_provisioning/configuration/"
                 "claimCertPath");
        static uint8_t claim_cert_path_mem[PATH_MAX] = { 0 };
        GglArena alloc = ggl_arena_init(ggl_buffer_substr(
            GGL_BUF(claim_cert_path_mem), 0, sizeof(claim_cert_path_mem) - 1
        ));
        GglBuffer claim_cert_path = { 0 };
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("claimCertPath")
            ),
            &alloc,
            &claim_cert_path
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        args->claim_cert_path = (char *) claim_cert_path.data;
    }

    if (args->claim_key_path == NULL) {
        GGL_LOGD("Requesting db for "
                 "services/aws.greengrass.fleet_provisioning/configuration/"
                 "claimKeyPath");
        static uint8_t claim_key_path_mem[PATH_MAX] = { 0 };
        GglArena alloc = ggl_arena_init(ggl_buffer_substr(
            GGL_BUF(claim_key_path_mem), 0, sizeof(claim_key_path_mem) - 1
        ));
        GglBuffer claim_key_path;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("claimKeyPath")
            ),
            &alloc,
            &claim_key_path
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        args->claim_key_path = (char *) claim_key_path_mem;
    }

    if (args->root_ca_path == NULL) {
        GGL_LOGD("Requesting db for "
                 "services/aws.greengrass.fleet_provisioning/configuration/"
                 "rootCaPath");
        static uint8_t root_ca_path_mem[PATH_MAX] = { 0 };
        GglArena alloc = ggl_arena_init(ggl_buffer_substr(
            GGL_BUF(root_ca_path_mem), 0, sizeof(root_ca_path_mem) - 1
        ));
        GglBuffer root_ca_path;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("rootCaPath")
            ),
            &alloc,
            &root_ca_path
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        args->root_ca_path = (char *) root_ca_path_mem;
    }

    if (args->data_endpoint == NULL) {
        GGL_LOGD("Requesting db for "
                 "services/aws.greengrass.fleet_provisioning/configuration/"
                 "iotDataEndpoint");
        static uint8_t data_endpoint_mem[MAX_ENDPOINT_LENGTH + 1] = { 0 };
        GglArena alloc = ggl_arena_init(ggl_buffer_substr(
            GGL_BUF(data_endpoint_mem), 0, sizeof(data_endpoint_mem) - 1
        ));
        GglBuffer data_endpoint;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("iotDataEndpoint")
            ),
            &alloc,
            &data_endpoint
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        args->data_endpoint = (char *) data_endpoint_mem;
    }

    if (args->template_name == NULL) {
        GGL_LOGD("Requesting db for "
                 "services/aws.greengrass.fleet_provisioning/configuration/"
                 "templateName");
        static uint8_t template_name_mem[MAX_TEMPLATE_LEN + 1] = { 0 };
        GglArena alloc = ggl_arena_init(ggl_buffer_substr(
            GGL_BUF(template_name_mem), 0, sizeof(template_name_mem) - 1
        ));
        GglBuffer template_name;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("templateName")
            ),
            &alloc,
            &template_name
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        args->template_name = (char *) template_name_mem;
    }

    if (args->template_parameters == NULL) {
        GGL_LOGD("Requesting db for "
                 "services/aws.greengrass.fleet_provisioning/configuration/"
                 "templateParams");
        static uint8_t template_params_mem[MAX_TEMPLATE_PARAM_LEN + 1] = { 0 };
        GglArena alloc = ggl_arena_init(ggl_buffer_substr(
            GGL_BUF(template_params_mem), 0, sizeof(template_params_mem) - 1
        ));
        GglBuffer template_params;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("templateParams")
            ),
            &alloc,
            &template_params
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        args->template_parameters = (char *) template_params_mem;
    }
    return GGL_ERR_OK;
}

static GglError cleanup_actions(void) {
    const char *args[]
        = { "chown", "-R", USER_GROUP, (char *) ggcredentials_path.data, NULL };

    GglError ret = ggl_exec_command(args);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to change ownership of certificates");
        return ret;
    }

    GGL_LOGI("Restarting all greengrass services to apply changes");

    const char *args_stop[]
        = { "systemctl", "stop", "greengrass-lite.target", NULL };
    ret = ggl_exec_command(args_stop);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to stop greengrass service");
        return ret;
    }
    const char *args_reset[] = { "systemctl", "reset-failed", NULL };
    ret = ggl_exec_command(args_reset);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to reset services run counter");
        return ret;
    }
    const char *args_start[]
        = { "systemctl", "start", "greengrass-lite.target", NULL };
    ret = ggl_exec_command(args_start);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to restart greengrass service");
        return ret;
    }

    return GGL_ERR_OK;
}

static GglError update_iot_endpoints(void) {
    static uint8_t endpoint_mem[2048] = { 0 };
    GglArena alloc = ggl_arena_init(GGL_BUF(endpoint_mem));
    GglBuffer data_endpoint;
    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.fleet_provisioning"),
            GGL_STR("configuration"),
            GGL_STR("iotDataEndpoint")
        ),
        &alloc,
        &data_endpoint
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("configuration"),
            GGL_STR("iotDataEndpoint")
        ),
        ggl_obj_buf(data_endpoint),
        &(int64_t) { 3 }
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    alloc = ggl_arena_init(GGL_BUF(endpoint_mem));
    GglBuffer cred_endpoint = GGL_BUF(endpoint_mem);
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.fleet_provisioning"),
            GGL_STR("configuration"),
            GGL_STR("iotCredEndpoint")
        ),
        &alloc,
        &cred_endpoint
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("configuration"),
            GGL_STR("iotCredEndpoint")
        ),
        ggl_obj_buf(cred_endpoint),
        &(int64_t) { 3 }
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}

GglError run_fleet_prov(FleetProvArgs *args, pid_t *pid) {
    // Check the config to see if we need to provision
    GglError ret = fetch_prov_status();
    if (ret != GGL_ERR_OK) {
        GGL_LOGI("Device already provisioned skipping.");
        return ret;
    }
    ret = check_if_claim_cert(*args);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Claim certs weren't accessable");
        return ret;
    }

    if (args->out_cert_path != NULL) {
        ggcredentials_path
            = (GglBuffer) { .data = (uint8_t *) args->out_cert_path,
                            .len = strlen(args->out_cert_path) };
    }

    {
        int config_dir;
        ret = ggl_dir_open(ggcredentials_path, O_RDONLY, false, &config_dir);
        if (ret != GGL_ERR_OK) {
            GGL_LOGI(
                "Could not open %.*s directory.",
                (int) ggcredentials_path.len,
                ggcredentials_path.data
            );
            return ret;
        }
        (void) ggl_close(config_dir);
    }

    ret = fetch_from_db(args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    EVP_PKEY *pkey = NULL;
    X509_REQ *csr_req = NULL;

    GGL_LOGD("Requesting db for system/rootPath");
    static uint8_t root_dir_mem[4096] = { 0 };
    GglArena alloc = ggl_arena_init(GGL_BUF(root_dir_mem));
    GglBuffer root_dir;
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootPath")), &alloc, &root_dir
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    pid_t iotcored_pid = -1;
    ret = start_iotcored(args, &iotcored_pid);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    *pid = iotcored_pid;

    static uint8_t private_file_path_mem[MAX_PATH_LEN] = { 0 };
    GglByteVec private_file_path_vec = GGL_BYTE_VEC(private_file_path_mem);
    ret = ggl_byte_vec_append(&private_file_path_vec, ggcredentials_path);
    ggl_byte_vec_chain_append(
        &ret, &private_file_path_vec, GGL_STR("/private_key.pem.key")
    );
    ggl_byte_vec_chain_push(&ret, &private_file_path_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error to append private key path");
        return ret;
    }

    static uint8_t public_file_path_mem[MAX_PATH_LEN] = { 0 };
    GglByteVec public_file_path_vec = GGL_BYTE_VEC(public_file_path_mem);
    ret = ggl_byte_vec_append(&public_file_path_vec, ggcredentials_path);
    ggl_byte_vec_chain_append(
        &ret, &public_file_path_vec, GGL_STR("/public_key.pem.key")
    );
    ggl_byte_vec_chain_push(&ret, &public_file_path_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error to append public key path");
        return ret;
    }

    static uint8_t cert_file_path_mem[MAX_PATH_LEN] = { 0 };
    GglByteVec cert_file_path_vec = GGL_BYTE_VEC(cert_file_path_mem);
    ret = ggl_byte_vec_append(&cert_file_path_vec, ggcredentials_path);
    ggl_byte_vec_chain_append(
        &ret, &cert_file_path_vec, GGL_STR("/certificate.pem.crt")
    );
    ggl_byte_vec_chain_push(&ret, &cert_file_path_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error to append certificate key path");
        return ret;
    }

    static uint8_t csr_file_path_mem[MAX_PATH_LEN] = { 0 };
    GglByteVec csr_file_path_vec = GGL_BYTE_VEC(csr_file_path_mem);
    ret = ggl_byte_vec_append(&csr_file_path_vec, ggcredentials_path);
    ggl_byte_vec_chain_append(&ret, &csr_file_path_vec, GGL_STR("/csr.pem"));
    ggl_byte_vec_chain_push(&ret, &csr_file_path_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error to append csr path");
        return ret;
    }

    ret = generate_key_files(
        pkey,
        csr_req,
        private_file_path_vec.buf,
        public_file_path_vec.buf,
        csr_file_path_vec.buf
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    EVP_PKEY_free(pkey);
    X509_REQ_free(csr_req);

    static uint8_t csr_mem[2048] = { 0 };

    // Try to read csr into memory
    int fd = -1;
    ret = ggl_file_open(csr_file_path_vec.buf, O_RDONLY, 0, &fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error opening csr file %d", ret);
        return ret;
    }

    GglBuffer csr_buf = GGL_BUF(csr_mem);
    ret = ggl_file_read(fd, &csr_buf);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to read csr file.");
        return GGL_ERR_FAILURE;
    }
    GGL_LOGD("CSR successfully read..");

    ret = make_request(csr_buf, cert_file_path_vec.buf, iotcored_pid);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // Copy root CA to provisioned-cert directory
    static uint8_t root_ca_dest_path_mem[MAX_PATH_LEN] = { 0 };
    GglByteVec root_ca_dest_path_vec = GGL_BYTE_VEC(root_ca_dest_path_mem);
    ret = ggl_byte_vec_append(&root_ca_dest_path_vec, ggcredentials_path);
    ggl_byte_vec_chain_append(
        &ret, &root_ca_dest_path_vec, GGL_STR("/AmazonRootCA.pem")
    );
    ggl_byte_vec_chain_push(&ret, &root_ca_dest_path_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error to append root CA dest path");
        return ret;
    }

    const char *cp_args[] = {
        "cp", args->root_ca_path, (char *) root_ca_dest_path_vec.buf.data, NULL
    };
    ret = ggl_exec_command(cp_args);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to copy root CA file");
        return ret;
    }

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("privateKeyPath")),
        ggl_obj_buf(ggl_buffer_substr(
            private_file_path_vec.buf, 0, private_file_path_vec.buf.len - 1
        )),
        &(int64_t) { 3 }
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootCaPath")),
        ggl_obj_buf((GglBuffer) { .data = root_ca_dest_path_vec.buf.data,
                                  .len = root_ca_dest_path_vec.buf.len - 1 }),
        &(int64_t) { 3 }
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = update_iot_endpoints();
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = cleanup_actions();
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}
