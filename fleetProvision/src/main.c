#include "fleetProvision.h"
#include <errno.h>
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/json_decode.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/socket.h>
#include <ggl/utils.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define KEY_LENGTH 2048

void add_extra_backslash(char *str);

static char global_cert_owenership[10024];
static char global_csr[10024];

static char global_thing_name[128];

static uint8_t big_buffer_for_bump[4096];
GglObject csr_payload_json_obj;

static const char *csr
    = "-----BEGIN CERTIFICATE "
      "REQUEST-----"
      "\nMIICqjCCAZICAQEwZTELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNhbGlmb3Jua"
      "W"
      "Ex\nFjAUBgNVBAcMDVNhbiBGcmFuY2lzY28xEjAQBgNVBAoMCU15Q29tcGFueTE"
      "V"
      "MBMG\nA1UEAwwMbXlkb21haW4uY29tMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AM"
      "I"
      "IBCgKC\nAQEAzq2MR0PBI6vQpk7GQ2iMkzV1YEDedFpobirt+"
      "NEzL1J4IWvJSxs3gW3Ohhcm\nzQTyrrV94Bn+"
      "RmNNK482VTW0YvrebWG6B3Bu8ABZAnnxtU2gYrlQOLXn5qd6Nwp3\nRtAnb+"
      "O019JytXjqi69oiTzC47Sysvp39rHeoOJGNE7ojPq5xXcxfxnXuOn26N1a\nnzu"
      "y"
      "EM7Gys0VmN9GmkP/"
      "nDpSw9ptu+GuVww4IZysWT6IPnrqbnWEVZZoyMk2LXSh\nd5S2L+2/"
      "2+g8p2bRN5cDYdYMDkixiTsp6+4qsFh8CTf5SaXedNPu5LIW0vYc/"
      "s2e\nv7iNRFf684M8UJekI4TBOR3m0wIDAQABoAAwDQYJKoZIhvcNAQELBQADgg"
      "E"
      "BALMo\nhriNmOoL9bkx9iHVkrh+"
      "3Mbj3LKrKxleWyQx6UAKfh0sXRtUjmrM3DOb6gyoXFwh\nLKMdg3NwHIhfMLLKV"
      "6"
      "HGbJOTwwo8mYcqzwEfQT7aPGKYFmsso8X5sAOh8jeIKQIC\n+"
      "sy06hBUQaPx2UBeYBelXEb9cz0WxuCXJ0kzdyBkYRtmmR2jNh4KzBcSA4ZwqQ+"
      "K\nI2+spnzb/PrA8pxiqrL03E3xl+mGqyLhbCcYmEbQbaEWp8/"
      "BXanDjSzcE4eDCyh/\nk9jbWnSwYII7PhwVhotYqg/"
      "fRanNOVNQ9bStDpjq4+umfE4hktKb0Ui6SSiJ9bur\nnmFsL7TQEsm4gBzrxdg="
      "\n-----END CERTIFICATE REQUEST-----\n";

const char *template_name = "FleetTest";

static const char *register_thing_success_url
    = "$aws/provisioning-templates/FleetTest/provision/json/rejected";

static const char *register_thing_url
    = "$aws/provisioning-templates/FleetTest/provision/json";

static const char certificate_response_url[]
    = "$aws/certificates/create-from-csr/json/accepted";

static const char *cert_request_url = "$aws/certificates/create-from-csr/json";

static const char *config_template_param_string
    = "{\"SerialNumber\": \"14ASSS55UUAA\"}";

static GglBuffer iotcored = GGL_STR("/aws/ggl/iotcored");

void add_extra_backslash(char *str) {
    size_t string_length = strlen(str);

    size_t placement = 0;
    while (str[placement] != '\0') {
        char val = str[placement];
        if (val == '\n') {
            memmove(str + placement + 2, str + placement + 1, string_length);
            str[placement] = '\\'; // Add the extra backslash
            str[placement + 1] = 'n';
            placement += 2;
        }
        placement++;
    }
}

static GglError subscribe_callback(void *ctx, uint32_t handle, GglObject data) {
    (void) ctx;
    (void) handle;

    if (data.type != GGL_TYPE_MAP) {
        GGL_LOGE("fleet-provisioning", "Subscription response is not a map.");
        return GGL_ERR_FAILURE;
    }

    GglBuffer topic = GGL_STR("");
    GglBuffer payload = GGL_STR("");

    GglObject *val;
    if (ggl_map_get(data.map, GGL_STR("topic"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE(
                "fleet-provisioning",
                "Subscription response topic not a buffer."
            );
            return GGL_ERR_FAILURE;
        }
        topic = val->buf;
    } else {
        GGL_LOGE(
            "fleet-provisioning", "Subscription response is missing topic."
        );
        return GGL_ERR_FAILURE;
    }
    if (ggl_map_get(data.map, GGL_STR("payload"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE(
                "fleet-provisioning",
                "Subscription response payload not a buffer."
            );
            return GGL_ERR_FAILURE;
        }
        payload = val->buf;
    } else {
        GGL_LOGE(
            "fleet-provisioning", "Subscription response is missing payload."
        );
        return GGL_ERR_FAILURE;
    }

    if (strncmp((char *) topic.data, certificate_response_url, topic.len)
        == 0) {
        GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

        memcpy(global_cert_owenership, payload.data, payload.len);

        GglBuffer response_buffer = (GglBuffer
        ) { .data = (uint8_t *) global_cert_owenership, .len = payload.len };

        ggl_json_decode_destructive(
            response_buffer, &balloc.alloc, &csr_payload_json_obj
        );

        if (csr_payload_json_obj.type != GGL_TYPE_MAP) {
            return GGL_ERR_FAILURE;
        }

        if (ggl_map_get(
                csr_payload_json_obj.map, GGL_STR("certificatePem"), &val
            )) {
            if (val->type != GGL_TYPE_BUF) {
                return GGL_ERR_PARSE;
            }
            int fd = open(
                "./certificate.crt",
                O_WRONLY | O_CREAT | O_CLOEXEC,
                S_IRUSR | S_IWUSR
            );
            if (fd < 0) {
                int err = errno;
                GGL_LOGE(
                    "fleet-provisioning",
                    "Failed to open certificate for writing: %d",
                    err
                );
                return GGL_ERR_FAILURE;
            }

            GglError ret = ggl_write_exact(fd, val->buf);
            close(fd);
            if (ret != GGL_ERR_OK) {
                return ret;
            }

            // Now find and save the value of certificateOwnershipToken
            if (ggl_map_get(
                    csr_payload_json_obj.map,
                    GGL_STR("certificateOwnershipToken"),
                    &val
                )) {
                if (val->type != GGL_TYPE_BUF) {
                    return GGL_ERR_PARSE;
                }
                memcpy(global_cert_owenership, val->buf.data, val->buf.len);
                GGL_LOGI(
                    "fleet-provisioning",
                    "Global Certificate Ownership Val %.*s",
                    (int) val->buf.len,
                    global_cert_owenership
                );
            }
        }
    }

    if (strncmp((char *) topic.data, register_thing_success_url, topic.len)
        == 0) {
        // GglBumpAlloc balloc =
        // ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

        memcpy(global_thing_name, payload.data, payload.len);
    }

    GGL_LOGI(
        "fleet-provisioning",
        "Got message from IoT Core; topic: %.*s, payload: %.*s.",
        (int) topic.len,
        topic.data,
        (int) payload.len,
        payload.data
    );

    return GGL_ERR_OK;
}

int request_thing_name() {
    static uint8_t temp_payload_alloc2[2000] = { 0 };

    GglBuffer thing_request_buf = GGL_BUF(temp_payload_alloc2);
    static char *template_buffer[100000];
    memcpy(
        template_buffer,
        config_template_param_string,
        strlen(config_template_param_string)
    );
    GglBuffer template_parameter_buffer = (GglBuffer
        ) { .data = (uint8_t *) template_buffer, .len = strlen(config_template_param_string)};

    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));
    GglObject config_template_param_json_obj;
    ggl_json_decode_destructive(
            template_parameter_buffer, &balloc.alloc, &config_template_param_json_obj
        );

    //Full Request Parameter Builder
    GglObject thing_payload_obj = GGL_OBJ_MAP(
        { GGL_STR("certificateOwnershipToken"),
          GGL_OBJ((GglBuffer) { .data = (uint8_t *) global_cert_owenership,
                                .len = strlen(global_cert_owenership) }) },
        { GGL_STR("parameters"),
           config_template_param_json_obj}
    );
    GglError ret_err_json = ggl_json_encode(thing_payload_obj, &thing_request_buf);
    if (ret_err_json != GGL_ERR_OK) {
        return GGL_ERR_PARSE;
    }

    //Publish message builder for thing request
    GglMap thing_request_args = GGL_MAP(
        { GGL_STR("topic"),
          GGL_OBJ((GglBuffer) { .len = strlen(register_thing_url),
                                .data = (uint8_t *) register_thing_url }) },
        { GGL_STR("payload"), GGL_OBJ(thing_request_buf) },
    );
    sleep(5);
    GglError ret_thing_req_publish
        = ggl_notify(iotcored, GGL_STR("publish"), thing_request_args);
    if (ret_thing_req_publish != 0) {
        GGL_LOGE(
            "fleet-provisioning",
            "Failed to send notify message to %.*s",
            (int) iotcored.len,
            iotcored.data
        );
        return EPROTO;
    }

    GGL_LOGI("fleet-provisioning", "Sent MQTT publish.");
}

int make_request(char *local_csr) {
    static uint8_t temp_payload_alloc[2000] = { 0 };
    

    GglBuffer csr_buf = GGL_BUF(temp_payload_alloc);

    GglObject csr_payload_obj
        = GGL_OBJ_MAP({ GGL_STR("certificateSigningRequest"),
                        GGL_OBJ((GglBuffer) { .data = (uint8_t *) local_csr,
                                              .len = strlen(local_csr) }) });
    GglError ret_err_json = ggl_json_encode(csr_payload_obj, &csr_buf);

    if (ret_err_json != GGL_ERR_OK) {
        return GGL_ERR_PARSE;
    }

    

    // Subscribe to csr success topic
    GglMap subscribe_args = GGL_MAP(
        { GGL_STR("topic_filter"),
          GGL_OBJ_STR(certificate_response_url) },
    );

    GglError ret = ggl_subscribe(
        iotcored,
        GGL_STR("subscribe"),
        subscribe_args,
        subscribe_callback,
        NULL,
        NULL,
        NULL,
        NULL
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "fleet-provisioning",
            "Failed to send notify message to %.*s",
            (int) iotcored.len,
            iotcored.data
        );
        return EPROTO;
    }
    GGL_LOGI(
        "fleet-provisioning",
        "Successfully sent certificate accepted subscription."
    );

    // Subscribe to register thing success topic
    GglMap subscribe_thing_args = GGL_MAP(
        { GGL_STR("topic_filter"),
          GGL_OBJ((GglBuffer) { .len = strlen(register_thing_success_url),
                                .data = (uint8_t *) register_thing_success_url }
          ) },
    );

    GglError return_thing_sub = ggl_subscribe(
        iotcored,
        GGL_STR("subscribe"),
        subscribe_thing_args,
        subscribe_callback,
        NULL,
        NULL,
        NULL,
        NULL
    );
    if (return_thing_sub != GGL_ERR_OK) {
        GGL_LOGE(
            "fleet-provisioning",
            "Failed to send thing accepted notify message to %.*s",
            (int) iotcored.len,
            iotcored.data
        );
        return EPROTO;
    }
    GGL_LOGI(
        "fleet-provisioning", "Successfully sent thing accepted subscription."
    );

    // // {
    // //     "certificateSigningRequest": "string"
    // // }
    // GglMap args = GGL_MAP(
    //     { GGL_STR("topic"),
    //       GGL_OBJ((GglBuffer) { .len = strlen(cert_request_url),
    //                             .data = (uint8_t *) cert_request_url }) },
    //     { GGL_STR("payload"), GGL_OBJ(csr_buf) },
    // );
    // sleep(5);
    // GglError ret_publish = ggl_notify(iotcored, GGL_STR("publish"), args);
    // if (ret_publish != 0) {
    //     GGL_LOGE(
    //         "fleet-provisioning",
    //         "Failed to send notify message to %.*s",
    //         (int) iotcored.len,
    //         iotcored.data
    //     );
    //     return EPROTO;
    // }

    // {
    //     "certificateOwnershipToken": "string",
    //     "parameters": {
    //         "string": "string",
    //         ...
    //     }
    // }

    

    sleep(300);
    return GGL_ERR_OK;
}

int main(void) {
    // EVP_PKEY *pkey = NULL;
    // X509_REQ *req = NULL;

    // generate_key_files(pkey, req);

    memcpy(global_csr, csr, strlen(csr));
    // add_extra_backslash(global_csr);

    GGL_LOGI(
        "fleet-provisioning",
        "New String: %.*s.",
        (int) strlen(global_csr),
        global_csr
    );

    make_request(global_csr);

    return 0;
}
