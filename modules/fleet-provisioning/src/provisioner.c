// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "provisioner.h"
#include <errno.h>
#include <fcntl.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/exec.h>
#include <ggl/file.h>
#include <ggl/json_decode.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/utils.h>
#include <ggl/vector.h>
#include <string.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define TEMPLATE_PARAM_BUFFER_SIZE 10000

static char global_thing_response_buf[512];
static char global_cert_ownership[10024];
static char global_register_thing_url[128] = { 0 };
static char global_register_thing_accept_url[128] = { 0 };
static char global_register_thing_reject_url[128] = { 0 };
static GglBuffer template_param;
static pid_t global_iotcored_pid;

static uint8_t arena_mem[4096];
GglObject csr_payload_json_obj;
char *global_cert_file_path;

atomic_bool complete_status = false;

static GglBuffer iotcored = GGL_STR("iotcoredfleet");

static const char *certificate_response_url
    = "$aws/certificates/create-from-csr/json/accepted";

static const char *certificate_response_reject_url
    = "$aws/certificates/create-from-csr/json/rejected";

static const char *cert_request_url = "$aws/certificates/create-from-csr/json";

static GglError request_thing_name(GglObject *cert_owner_gg_obj) {
    static uint8_t temp_payload_alloc2[2000] = { 0 };

    GglBuffer thing_request_buf = GGL_BUF(temp_payload_alloc2);

    GglArena arena = ggl_arena_init(GGL_BUF(arena_mem));
    GglObject config_template_param_json_obj;
    GglError json_status = ggl_json_decode_destructive(
        template_param, &arena, &config_template_param_json_obj
    );

    if (json_status != GGL_ERR_OK
        && ggl_obj_type(config_template_param_json_obj) != GGL_TYPE_MAP) {
        GGL_LOGI(
            "Provided Parameter is not in Json format: %.*s",
            (int) template_param.len,
            template_param.data
        );
        return GGL_ERR_PARSE;
    }

    // Full Request Parameter Builder
    //
    // {
    //     "certificateOwnershipToken": "string",
    //     "parameters": {
    //         "string": "string",
    //         ...
    //     }
    // }
    GglObject thing_payload_obj = ggl_obj_map(GGL_MAP(
        { GGL_STR("certificateOwnershipToken"), *cert_owner_gg_obj },
        { GGL_STR("parameters"), config_template_param_json_obj }
    ));
    GglError ret_err_json
        = ggl_json_encode(thing_payload_obj, &thing_request_buf);
    if (ret_err_json != GGL_ERR_OK) {
        return GGL_ERR_PARSE;
    }

    // Publish message builder for thing request
    GglMap thing_request_args = GGL_MAP(
        { GGL_STR("topic"),
          ggl_obj_buf((GglBuffer
          ) { .len = strlen(global_register_thing_url),
              .data = (uint8_t *) global_register_thing_url }) },
        { GGL_STR("payload"), ggl_obj_buf(thing_request_buf) },
    );

    GglError ret_thing_req_publish
        = ggl_notify(iotcored, GGL_STR("publish"), thing_request_args);
    if (ret_thing_req_publish != 0) {
        GGL_LOGE(
            "Failed to send notify message to %.*s",
            (int) iotcored.len,
            iotcored.data
        );
        return GGL_ERR_FAILURE;
    }

    GGL_LOGI("Sent MQTT thing Register publish.");
    return GGL_ERR_OK;
}

static GglError set_global_values(pid_t iotcored_pid) {
    static char *template_url_prefix = "$aws/provisioning-templates/";
    global_iotcored_pid = iotcored_pid;

    // Fetch Template Name from db
    // TODO: Use args passed from entry.c
    static uint8_t template_name_mem[128];
    GglArena template_name_alloc = ggl_arena_init(GGL_BUF(template_name_mem));
    GglBuffer template_name;
    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.fleet_provisioning"),
            GGL_STR("configuration"),
            GGL_STR("templateName")
        ),
        &template_name_alloc,
        &template_name
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglByteVec register_thing_url = GGL_BYTE_VEC(global_register_thing_url);
    ggl_byte_vec_chain_append(
        &ret,
        &register_thing_url,
        ggl_buffer_from_null_term(template_url_prefix)
    );
    ggl_byte_vec_chain_append(&ret, &register_thing_url, template_name);
    ggl_byte_vec_chain_append(
        &ret, &register_thing_url, GGL_STR("/provision/json")
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to build register thing topic.");
        return GGL_ERR_NOMEM;
    }

    // Copy the prefix over to both buffer
    // Add success suffix
    GglByteVec register_thing_accept_url
        = GGL_BYTE_VEC(global_register_thing_accept_url);
    ggl_byte_vec_chain_append(
        &ret, &register_thing_accept_url, register_thing_url.buf
    );
    ggl_byte_vec_chain_append(
        &ret, &register_thing_accept_url, GGL_STR("/accepted")
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to build register thing accept topic.");
        return GGL_ERR_NOMEM;
    }
    // Add failure suffix
    GglByteVec register_thing_reject_url
        = GGL_BYTE_VEC(global_register_thing_reject_url);
    ggl_byte_vec_chain_append(
        &ret, &register_thing_reject_url, register_thing_url.buf
    );
    ggl_byte_vec_chain_append(
        &ret, &register_thing_reject_url, GGL_STR("/rejected")
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to build register thing reject topic.");
        return GGL_ERR_NOMEM;
    }

    static uint8_t template_param_mem[TEMPLATE_PARAM_BUFFER_SIZE];
    GglArena template_param_alloc = ggl_arena_init(GGL_BUF(template_param_mem));

    // Fetch Template Parameters
    // TODO: Use args passed from entry.c
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.fleet_provisioning"),
            GGL_STR("configuration"),
            GGL_STR("templateParams")
        ),
        &template_param_alloc,
        &template_param
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}

// TODO: Refactor this function
//  NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GglError subscribe_callback(void *ctx, uint32_t handle, GglObject data) {
    (void) ctx;
    (void) handle;

    GglBuffer topic;
    GglBuffer payload;

    GglError ret
        = ggl_aws_iot_mqtt_subscribe_parse_resp(data, &topic, &payload);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_LOGI(
        "Got message from IoT Core; topic: %.*s, payload: %.*s.",
        (int) topic.len,
        topic.data,
        (int) payload.len,
        payload.data
    );

    if (ggl_buffer_eq(
            topic, ggl_buffer_from_null_term((char *) certificate_response_url)
        )) {
        GglArena arena = ggl_arena_init(GGL_BUF(arena_mem));

        memcpy(global_cert_ownership, payload.data, payload.len);

        GglBuffer response_buffer = (GglBuffer
        ) { .data = (uint8_t *) global_cert_ownership, .len = payload.len };

        ret = ggl_json_decode_destructive(
            response_buffer, &arena, &csr_payload_json_obj
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        if (ggl_obj_type(csr_payload_json_obj) != GGL_TYPE_MAP) {
            return GGL_ERR_FAILURE;
        }

        GglObject *val;
        if (ggl_map_get(
                ggl_obj_into_map(csr_payload_json_obj),
                GGL_STR("certificatePem"),
                &val
            )) {
            if (ggl_obj_type(*val) != GGL_TYPE_BUF) {
                return GGL_ERR_PARSE;
            }
            int fd = open(
                global_cert_file_path,
                O_WRONLY | O_CREAT | O_CLOEXEC | O_TRUNC,
                S_IRUSR | S_IWUSR
            );
            if (fd < 0) {
                int err = errno;
                GGL_LOGE("Failed to open certificate for writing: %d", err);
                return GGL_ERR_FAILURE;
            }

            ret = ggl_file_write(fd, ggl_obj_into_buf(*val));
            (void) ggl_close(fd);
            if (ret != GGL_ERR_OK) {
                return ret;
            }

            ret = ggl_gg_config_write(
                GGL_BUF_LIST(GGL_STR("system"), GGL_STR("certificateFilePath")),
                ggl_obj_buf((GglBuffer
                ) { .data = (uint8_t *) global_cert_file_path,
                    .len = strlen(global_cert_file_path) }),
                &(int64_t) { 3 }
            );
            if (ret != GGL_ERR_OK) {
                return ret;
            }

            // Now find and save the value of certificateOwnershipToken
            if (ggl_map_get(
                    ggl_obj_into_map(csr_payload_json_obj),
                    GGL_STR("certificateOwnershipToken"),
                    &val
                )) {
                if (ggl_obj_type(*val) != GGL_TYPE_BUF) {
                    return GGL_ERR_PARSE;
                }
                memcpy(
                    global_cert_ownership,
                    ggl_obj_into_buf(*val).data,
                    ggl_obj_into_buf(*val).len
                );

                GGL_LOGI(
                    "Global Certificate Ownership Val %.*s",
                    (int) ggl_obj_into_buf(*val).len,
                    global_cert_ownership
                );

                // Now that we have a certificate make a call to register a
                // thing based on that certificate
                ret = request_thing_name(val);
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE("Requesting thing name failed");
                    return ret;
                }
            }
        }
    } else if (ggl_buffer_eq(
                   topic,
                   ggl_buffer_from_null_term(global_register_thing_accept_url)
               )) {
        GglArena alloc = ggl_arena_init(GGL_BUF(arena_mem));

        memcpy(global_thing_response_buf, payload.data, payload.len);

        GglBuffer response_buffer = (GglBuffer
        ) { .data = (uint8_t *) global_thing_response_buf, .len = payload.len };
        GglObject thing_payload_json_obj;

        ret = ggl_json_decode_destructive(
            response_buffer, &alloc, &thing_payload_json_obj
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        if (ggl_obj_type(thing_payload_json_obj) != GGL_TYPE_MAP) {
            return GGL_ERR_FAILURE;
        }

        GglObject *val;
        if (ggl_map_get(
                ggl_obj_into_map(thing_payload_json_obj),
                GGL_STR("thingName"),
                &val
            )) {
            ret = ggl_gg_config_write(
                GGL_BUF_LIST(GGL_STR("system"), GGL_STR("thingName")),
                *val,
                &(int64_t) { 3 }
            );
            if (ret != GGL_ERR_OK) {
                return ret;
            }

            // Stop iotcored here
            GGL_LOGI("Process Complete, Your device is now provisioned");
            (void) ggl_exec_kill_process(global_iotcored_pid);

            // TODO: Find a way to terminate cleanly with iotcored
            atomic_store(&complete_status, true);
        }
    } else {
        GGL_LOGI(
            "Got message from IoT Core; topic: %.*s, payload: %.*s.",
            (int) topic.len,
            topic.data,
            (int) payload.len,
            payload.data
        );
    }

    return GGL_ERR_OK;
}

GglError make_request(
    GglBuffer csr_as_ggl_buffer, GglBuffer cert_file_path, pid_t iotcored_pid
) {
    global_cert_file_path = (char *) cert_file_path.data;

    GglError ret = set_global_values(iotcored_pid);
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }

    static uint8_t temp_payload_alloc[2000] = { 0 };

    GglBuffer csr_buf = GGL_BUF(temp_payload_alloc);

    // Subscribe to csr success topic
    GglMap subscribe_args = GGL_MAP(
        { GGL_STR("topic_filter"),
          ggl_obj_buf((GglBuffer
          ) { .data = (uint8_t *) certificate_response_url,
              .len = strlen(certificate_response_url) }) },
    );

    ret = ggl_subscribe(
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
            "Failed to send notify message to %.*s, Error: %d",
            (int) iotcored.len,
            iotcored.data,
            EPROTO
        );
        return GGL_ERR_FAILURE;
    }
    GGL_LOGI("Successfully set csr accepted subscription.");

    (void) ggl_sleep(2);

    // Subscribe to csr reject topic
    GglMap subscribe_reject_args = GGL_MAP(
        { GGL_STR("topic_filter"),
          ggl_obj_buf((GglBuffer
          ) { .data = (uint8_t *) certificate_response_reject_url,
              .len = strlen(certificate_response_reject_url) }) },
    );

    ret = ggl_subscribe(
        iotcored,
        GGL_STR("subscribe"),
        subscribe_reject_args,
        subscribe_callback,
        NULL,
        NULL,
        NULL,
        NULL
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Failed to send notify message to %.*s, Error: %d",
            (int) iotcored.len,
            iotcored.data,
            EPROTO
        );
        return GGL_ERR_FAILURE;
    }
    GGL_LOGI("Successfully set csr rejected subscription.");

    (void) ggl_sleep(2);

    // Subscribe to register thing success topic
    GglMap subscribe_thing_args = GGL_MAP(
        { GGL_STR("topic_filter"),
          ggl_obj_buf((GglBuffer
          ) { .len = strlen(global_register_thing_accept_url),
              .data = (uint8_t *) global_register_thing_accept_url }) },
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
            "Failed to send thing accepted notify message to %.*s, Error: %d",
            (int) iotcored.len,
            iotcored.data,
            EPROTO
        );
        return GGL_ERR_FAILURE;
    }
    GGL_LOGI("Successfully set thing accepted subscription.");

    // Subscribe to register thing success topic
    GglMap subscribe_thing_reject_args = GGL_MAP(
        { GGL_STR("topic_filter"),
          ggl_obj_buf((GglBuffer
          ) { .len = strlen(global_register_thing_reject_url),
              .data = (uint8_t *) global_register_thing_reject_url }) },
    );

    GglError return_thing_sub_reject = ggl_subscribe(
        iotcored,
        GGL_STR("subscribe"),
        subscribe_thing_reject_args,
        subscribe_callback,
        NULL,
        NULL,
        NULL,
        NULL
    );
    if (return_thing_sub_reject != GGL_ERR_OK) {
        GGL_LOGE(
            "Failed to send thing accepted notify message to %.*s, Error: %d",
            (int) iotcored.len,
            iotcored.data,
            EPROTO
        );
        return GGL_ERR_FAILURE;
    }
    GGL_LOGI("Successfully set thing rejected subscription.");

    (void) ggl_sleep(2);

    // Create a json payload object
    GglObject csr_payload_obj
        = ggl_obj_map(GGL_MAP({ GGL_STR("certificateSigningRequest"),
                                ggl_obj_buf(csr_as_ggl_buffer) }));
    GglError ret_err_json = ggl_json_encode(csr_payload_obj, &csr_buf);
    if (ret_err_json != GGL_ERR_OK) {
        return GGL_ERR_PARSE;
    }

    // {
    //     "certificateSigningRequest": "string"
    // }
    // Prepare publish packet for requesting certificate with csr
    GglMap args = GGL_MAP(
        { GGL_STR("topic"),
          ggl_obj_buf((GglBuffer) { .len = strlen(cert_request_url),
                                    .data = (uint8_t *) cert_request_url }) },
        { GGL_STR("payload"), ggl_obj_buf(csr_buf) },
    );

    (void) ggl_sleep(2);

    // Make Publish request to get the new certificate
    GglError ret_publish = ggl_notify(iotcored, GGL_STR("publish"), args);
    if (ret_publish != 0) {
        GGL_LOGE(
            "Failed to send notify message to %.*s, Error:%d",
            (int) iotcored.len,
            iotcored.data,
            EPROTO
        );
        return GGL_ERR_FAILURE;
    }

    while (!atomic_load(&complete_status)) { // Continuously check the flag
        GGL_LOGI("Wating for thing to register");
        (void) ggl_sleep(5);
    }

    return GGL_ERR_OK;
}
