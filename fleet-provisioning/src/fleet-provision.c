#include "fleet-provision.h"
#include <asm-generic/errno.h>
#include <errno.h>
#include <fcntl.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/json_decode.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/socket.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#define TEMPLATE_PARAM_BUFFER_SIZE 10000

static char global_thing_response_buf[512];
static char global_cert_owenership[10024];
static char global_register_thing_url[128] = { 0 };
static char global_register_thing_accept_url[128] = { 0 };
static char global_register_thing_reject_url[128] = { 0 };
static char template_param_buffer_alloc[TEMPLATE_PARAM_BUFFER_SIZE];

static uint8_t big_buffer_for_bump[4096];
GglObject csr_payload_json_obj;

static GglBuffer iotcored = GGL_STR("/aws/ggl/iotcored");
static GglBuffer ggconfigd = GGL_STR("/aws/ggl/ggconfigd");

static const char *certificate_response_url
    = "$aws/certificates/create-from-csr/json/accepted";

static const char *certificate_response_reject_url
    = "$aws/certificates/create-from-csr/json/rejected";

static const char *cert_request_url = "$aws/certificates/create-from-csr/json";

// static const char *config_template_param_string
//     = "{\"SerialNumber\": \"14ASSS55UUAA\"}";

// Static Function Declaration
static int set_global_values(void);
static int request_thing_name(GglObject *cert_owner_gg_obj);
static GglError subscribe_callback(void *ctx, uint32_t handle, GglObject data);
static void get_value_from_db(
    GglBuffer component,
    GglBuffer test_key,
    GglBumpAlloc the_allocator,
    char *return_string
);
static void save_value_to_db(
    GglObject component, GglObject key, GglObject value
);

// End Static Function Declaration

static void get_value_from_db(
    GglBuffer component,
    GglBuffer test_key,
    GglBumpAlloc the_allocator,
    char *return_string
) {
    GglMap params = GGL_MAP(
        { GGL_STR("component"), GGL_OBJ(component) },
        { GGL_STR("key"), GGL_OBJ(test_key) },
    );
    GglObject result;

    GglError error = ggl_call(
        ggconfigd, GGL_STR("read"), params, NULL, &the_allocator.alloc, &result
    );
    if (error != GGL_ERR_OK) {
        GGL_LOGE(
            "fleet-provisioning",
            "%.*s read failed. Error %d",
            (int) component.len,
            component.data,
            error
        );
    } else {
        memcpy(return_string, result.buf.data, result.buf.len);

        if (result.type == GGL_TYPE_BUF) {
            GGL_LOGI(
                "fleet-provisioning",
                "read value: %.*s",
                (int) result.buf.len,
                (char *) result.buf.data
            );
        }
    }
}

static void save_value_to_db(
    GglObject component, GglObject key, GglObject value
) {
    GglBuffer server = GGL_STR("/aws/ggl/ggconfigd");

    static uint8_t big_buffer_transfer_for_bump[256] = { 0 };
    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_transfer_for_bump));

    GglMap params = GGL_MAP(
        { GGL_STR("component"), component },
        { GGL_STR("key"), key },
        { GGL_STR("value"), value }
    );
    GglObject result;

    GglError error = ggl_call(
        server, GGL_STR("write"), params, NULL, &the_allocator.alloc, &result
    );

    if (error != GGL_ERR_OK) {
        GGL_LOGE("ggconfig test", "insert failure");
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

                // Now that we have a certificate make a call to register a
                // thing based on that certificate
                request_thing_name(val);
            }
        }
    }

    else if (strncmp(
            (char *) topic.data, global_register_thing_accept_url, topic.len
        )
        == 0) {
        GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

        memcpy(global_thing_response_buf, payload.data, payload.len);

        GglBuffer response_buffer = (GglBuffer
        ) { .data = (uint8_t *) global_cert_owenership, .len = payload.len };
        GglObject thing_payload_json_obj;

        ggl_json_decode_destructive(
            response_buffer, &balloc.alloc, &thing_payload_json_obj
        );
        if (thing_payload_json_obj.type != GGL_TYPE_MAP) {
            return GGL_ERR_FAILURE;
        }

        if (ggl_map_get(
                thing_payload_json_obj.map, GGL_STR("thingName"), &val
            )) {
            save_value_to_db(GGL_OBJ_STR("system"), GGL_OBJ_STR("thingName"), *val);
        }

    } else {
        GGL_LOGI(
            "fleet-provisioning",
            "Got message from IoT Core; topic: %.*s, payload: %.*s.",
            (int) topic.len,
            topic.data,
            (int) payload.len,
            payload.data
        );
    }

    return GGL_ERR_OK;
}

static int request_thing_name(GglObject *cert_owner_gg_obj) {
    static uint8_t temp_payload_alloc2[2000] = { 0 };

    GglBuffer thing_request_buf = GGL_BUF(temp_payload_alloc2);

    GglBuffer template_parameter_buffer
        = (GglBuffer) { .data = (uint8_t *) template_param_buffer_alloc,
                        .len = strlen(template_param_buffer_alloc) };

    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));
    GglObject config_template_param_json_obj;
    GglError json_status = ggl_json_decode_destructive(
        template_parameter_buffer,
        &balloc.alloc,
        &config_template_param_json_obj
    );

    if (json_status != GGL_ERR_OK
        && config_template_param_json_obj.type != GGL_TYPE_MAP) {
        GGL_LOGI(
            "fleet-provisioning",
            "Provided Parameter is not in Json format: %.*s",
            (int) strlen(template_param_buffer_alloc),
            template_param_buffer_alloc
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
    GglObject thing_payload_obj = GGL_OBJ_MAP(
        { GGL_STR("certificateOwnershipToken"), *cert_owner_gg_obj },
        { GGL_STR("parameters"), config_template_param_json_obj }
    );
    GglError ret_err_json
        = ggl_json_encode(thing_payload_obj, &thing_request_buf);
    if (ret_err_json != GGL_ERR_OK) {
        return GGL_ERR_PARSE;
    }

    // Publish message builder for thing request
    GglMap thing_request_args = GGL_MAP(
        { GGL_STR("topic"),
          GGL_OBJ((GglBuffer) { .len = strlen(global_register_thing_url),
                                .data = (uint8_t *) global_register_thing_url }
          ) },
        { GGL_STR("payload"), GGL_OBJ(thing_request_buf) },
    );

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

    GGL_LOGI("fleet-provisioning", "Sent MQTT thing Register publish.");
    return GGL_ERR_OK;
}

static int set_global_values(void) {
    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

    static char *template_url_prefix = "$aws/provisioning-templates/";
    static char template_name_local_buf[64] = { 0 };

    // Fetch Template Name from db
    get_value_from_db(
        GGL_STR("fleet-provisioning"),
        GGL_STR("templateName"),
        the_allocator,
        template_name_local_buf
    );

    if (strlen(template_name_local_buf) == 0) {
        GGL_LOGE(
            "fleet-provisioning", "Failed to fetch template name from database"
        );
        return GGL_ERR_FAILURE;
    }

    strncat(
        global_register_thing_url,
        template_url_prefix,
        strlen(template_url_prefix)
    );
    strncat(
        global_register_thing_url,
        template_name_local_buf,
        strlen(template_name_local_buf)
    );
    strncat(
        global_register_thing_url, "provision/json", strlen("provision/json")
    );

    // Copy the prefix over to both buffer
    // Add success suffix
    strcat(global_register_thing_accept_url, global_register_thing_url);
    strcat(global_register_thing_accept_url, "/accepted\0");
    // Add failure suffix
    strcat(global_register_thing_reject_url, global_register_thing_url);
    strcat(global_register_thing_reject_url, "/rejected\0");

    // Fetch Template Parameters
    get_value_from_db(
        GGL_STR("fleet-provisioning"),
        GGL_STR("templateParams"),
        the_allocator,
        template_param_buffer_alloc
    );

    GGL_LOGD(
        "fleet-provisioning",
        "Template parameters Fetched: %.*s",
        (int) strlen(template_param_buffer_alloc),
        template_param_buffer_alloc
    );
    return GGL_ERR_OK;
}

int make_request(char *csr_as_string) {
    int ret_db = set_global_values();

    if (ret_db != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }

    static uint8_t temp_payload_alloc[2000] = { 0 };

    GglBuffer csr_buf = GGL_BUF(temp_payload_alloc);

    // Subscribe to csr success topic
    GglMap subscribe_args = GGL_MAP(
        { GGL_STR("topic_filter"),
          GGL_OBJ((GglBuffer) { .data = (uint8_t *) certificate_response_url,
                                .len = strlen(certificate_response_url) }) },
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
        "fleet-provisioning", "Successfully set csr accepted subscription."
    );

    sleep(2);

    // Subscribe to csr reject topic
    GglMap subscribe_reject_args = GGL_MAP(
        { GGL_STR("topic_filter"),
          GGL_OBJ((GglBuffer
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
            "fleet-provisioning",
            "Failed to send notify message to %.*s",
            (int) iotcored.len,
            iotcored.data
        );
        return EPROTO;
    }
    GGL_LOGI(
        "fleet-provisioning", "Successfully set csr rejected subscription."
    );

    sleep(2);

    // Subscribe to register thing success topic
    GglMap subscribe_thing_args = GGL_MAP(
        { GGL_STR("topic_filter"),
          GGL_OBJ((GglBuffer
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
            "fleet-provisioning",
            "Failed to send thing accepted notify message to %.*s",
            (int) iotcored.len,
            iotcored.data
        );
        return EPROTO;
    }
    GGL_LOGI(
        "fleet-provisioning", "Successfully set thing accepted subscription."
    );

    sleep(2);

    // Create a json payload object
    GglObject csr_payload_obj
        = GGL_OBJ_MAP({ GGL_STR("certificateSigningRequest"),
                        GGL_OBJ((GglBuffer) { .data = (uint8_t *) csr_as_string,
                                              .len = strlen(csr_as_string) }) }
        );
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
          GGL_OBJ((GglBuffer) { .len = strlen(cert_request_url),
                                .data = (uint8_t *) cert_request_url }) },
        { GGL_STR("payload"), GGL_OBJ(csr_buf) },
    );
    sleep(5);

    // Make Publish request to get the new certificate
    GglError ret_publish = ggl_notify(iotcored, GGL_STR("publish"), args);
    if (ret_publish != 0) {
        GGL_LOGE(
            "fleet-provisioning",
            "Failed to send notify message to %.*s",
            (int) iotcored.len,
            iotcored.data
        );
        return EPROTO;
    }

    sleep(300);
    return GGL_ERR_OK;
}