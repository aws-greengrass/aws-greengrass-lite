#include "cloud_request.h"
#include "ggl/json_encode.h"
#include "ggl/vector.h"
#include <ggl/arena.h>
#include <ggl/aws_iot_call.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/io.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_TOKEN_SIZE 512
#define MAX_TOPIC_LEN 256
#define MAX_REQUEST_RESPONSE_SIZE 4096

// Based on
// https://docs.aws.amazon.com/iot/latest/apireference/API_CreateCertificateFromCsr.html
// 20480(approx down)+ 64 + 2048 + 256 = 22848 Bytes
// Max certificatePem + Fixed certificateId + Max certificateArn + json
// formatting Next reasonable size: 24KB
#define MAX_CSR_RESPONSE_SIZE 24576

// Based on
// https://docs.aws.amazon.com/iot/latest/apireference/API_RegisterThing.html
// Assuming reasonable as MAX templatebody + 1 MAX paramKey + 1 MAX paramValue
#define MAX_REGISTER_THING_PAYLOAD_SIZE 16384

static GglError send_csr_request(
    GglBuffer csr_as_ggl_buffer,
    GglBuffer *token_out,
    GglBuffer iotcored,
    int certificate_fd
) {
    uint8_t arena_mem[MAX_CSR_RESPONSE_SIZE] = { 0 };
    GglArena arena = ggl_arena_init(GGL_BUF(arena_mem));

    GglObject csr_payload_obj = ggl_obj_map(GGL_MAP(ggl_kv(
        GGL_STR("certificateSigningRequest"), ggl_obj_buf(csr_as_ggl_buffer)
    )));

    GglObject result;
    GglError ret = ggl_aws_iot_call(
        iotcored,
        GGL_STR("$aws/certificates/create-from-csr/json"),
        csr_payload_obj,
        true,
        &arena,
        &result
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglObject *token_val;
    if (!ggl_map_get(
            ggl_obj_into_map(result),
            GGL_STR("certificateOwnershipToken"),
            &token_val
        )) {
        uint8_t json_error_response[MAX_REQUEST_RESPONSE_SIZE] = { 0 };
        GglBuffer json_error_response_buf = GGL_BUF(json_error_response);
        (void
        ) ggl_json_encode(result, ggl_buf_writer(&json_error_response_buf));
        GGL_LOGE(
            "Failed to register certificate. Response:  %.*s",
            (int) json_error_response_buf.len,
            json_error_response_buf.data
        );
        return GGL_ERR_INVALID;
    }

    if (ggl_obj_type(*token_val) != GGL_TYPE_BUF) {
        GGL_LOGE("Failed to register certificate. Reason: Invalid "
                 "certificateOwnershipToken.");
        return GGL_ERR_INVALID;
    }

    GglBuffer token = ggl_obj_into_buf(*token_val);
    ret = ggl_buf_copy(token, token_out);
    token_out->len = token.len;

    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to copy token over");
        return ret;
    }

    // Extract and write certificatePem to file descriptor
    GglObject *cert_pem_val;
    if (!ggl_map_get(
            ggl_obj_into_map(result), GGL_STR("certificatePem"), &cert_pem_val
        )) {
        GGL_LOGE("Failed to get certificatePem from response.");
        return GGL_ERR_INVALID;
    }

    if (ggl_obj_type(*cert_pem_val) != GGL_TYPE_BUF) {
        GGL_LOGE("Invalid certificatePem type in response.");
        return GGL_ERR_INVALID;
    }

    GglBuffer cert_pem = ggl_obj_into_buf(*cert_pem_val);
    ssize_t written = write(certificate_fd, cert_pem.data, cert_pem.len);
    if (written != (ssize_t) cert_pem.len) {
        GGL_LOGE("Failed to write certificate to file.");
        return GGL_ERR_FAILURE;
    }

    GGL_LOGD("Certificate ownership token received (length: %zu)", token.len);
    return GGL_ERR_OK;
}

static GglError register_thing_name_request(
    GglBuffer template_name,
    GglMap template_params,
    GglBuffer token,
    GglBuffer iotcored,
    GglBuffer *thing_name_out
) {
    uint8_t arena_mem[MAX_REGISTER_THING_PAYLOAD_SIZE];
    GglArena arena = ggl_arena_init(GGL_BUF(arena_mem));

    GglObject thing_payload_obj = ggl_obj_map(GGL_MAP(
        ggl_kv(GGL_STR("certificateOwnershipToken"), ggl_obj_buf(token)),
        ggl_kv(GGL_STR("parameters"), ggl_obj_map(template_params))
    ));

    uint8_t topic_mem[MAX_TOPIC_LEN];
    GglByteVec topic_vec = GGL_BYTE_VEC(topic_mem);
    ggl_byte_vec_chain_append(
        &(GglError) { GGL_ERR_OK },
        &topic_vec,
        GGL_STR("$aws/provisioning-templates/")
    );
    ggl_byte_vec_chain_append(
        &(GglError) { GGL_ERR_OK }, &topic_vec, template_name
    );
    ggl_byte_vec_chain_append(
        &(GglError) { GGL_ERR_OK }, &topic_vec, GGL_STR("/provision/json")
    );

    GglObject result = { 0 };
    GglError ret = ggl_aws_iot_call(
        iotcored, topic_vec.buf, thing_payload_obj, true, &arena, &result
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglObject *thing_name_val = { 0 };
    if (!ggl_map_get(
            ggl_obj_into_map(result), GGL_STR("thingName"), &thing_name_val
        )) {
        uint8_t json_error_response[MAX_REQUEST_RESPONSE_SIZE] = { 0 };
        GglBuffer json_error_response_buf = GGL_BUF(json_error_response);
        (void
        ) ggl_json_encode(result, ggl_buf_writer(&json_error_response_buf));
        GGL_LOGE(
            "Failed to get thing name from response. Response: (%.*s)",
            (int) json_error_response_buf.len,
            json_error_response_buf.data
        );

        return GGL_ERR_INVALID;
    }

    if (ggl_obj_type(*thing_name_val) != GGL_TYPE_BUF) {
        GGL_LOGE("Invalid thing name type in response.");
        return GGL_ERR_INVALID;
    }

    GglBuffer thing_name = ggl_obj_into_buf(*thing_name_val);

    ret = ggl_buf_copy(thing_name, thing_name_out);
    thing_name_out->len = thing_name.len;
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Failed to copy thingName into the out buffer. Error: %d", ret
        );
        return ret;
    }

    GGL_LOGI(
        "Thing name received: %.*s", (int) thing_name.len, thing_name.data
    );
    return GGL_ERR_OK;
}

GglError ggl_get_certificate_from_aws(
    GglBuffer csr_as_ggl_buffer,
    GglBuffer template_name,
    GglMap template_params,
    GglBuffer *thing_name_out,
    int certificate_fd
) {
    static uint8_t token_mem[MAX_TOKEN_SIZE];
    GglBuffer token = GGL_BUF(token_mem);
    GglBuffer iotcored = GGL_STR("iotcoredfleet");

    GglError ret
        = send_csr_request(csr_as_ggl_buffer, &token, iotcored, certificate_fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return register_thing_name_request(
        template_name, template_params, token, iotcored, thing_name_out
    );
}
