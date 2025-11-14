#include "cloud_request.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/io.h>
#include <gg/json_encode.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/vector.h>
#include <ggl/aws_iot_call.h>
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

static GgError send_csr_request(
    GgBuffer csr_as_ggl_buffer,
    GgBuffer *token_out,
    GgBuffer iotcored,
    int certificate_fd
) {
    uint8_t arena_mem[MAX_CSR_RESPONSE_SIZE] = { 0 };
    GgArena arena = gg_arena_init(GG_BUF(arena_mem));

    GgObject csr_payload_obj = gg_obj_map(GG_MAP(gg_kv(
        GG_STR("certificateSigningRequest"), gg_obj_buf(csr_as_ggl_buffer)
    )));

    GgObject result;
    GgError ret = ggl_aws_iot_call(
        iotcored,
        GG_STR("$aws/certificates/create-from-csr/json"),
        csr_payload_obj,
        true,
        &arena,
        &result
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GgObject *token_val;
    if (!gg_map_get(
            gg_obj_into_map(result),
            GG_STR("certificateOwnershipToken"),
            &token_val
        )) {
        uint8_t json_error_response[MAX_REQUEST_RESPONSE_SIZE] = { 0 };
        GgBuffer json_error_response_buf = GG_BUF(json_error_response);
        (void) gg_json_encode(result, gg_buf_writer(&json_error_response_buf));
        GG_LOGE(
            "Failed to register certificate. Response:  %.*s",
            (int) json_error_response_buf.len,
            json_error_response_buf.data
        );
        return GG_ERR_INVALID;
    }

    if (gg_obj_type(*token_val) != GG_TYPE_BUF) {
        GG_LOGE(
            "Failed to register certificate. Reason: Invalid certificateOwnershipToken."
        );
        return GG_ERR_INVALID;
    }

    GgBuffer token = gg_obj_into_buf(*token_val);
    ret = gg_buf_copy(token, token_out);

    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to copy token over");
        return ret;
    }

    // Extract and write certificatePem to file descriptor
    GgObject *cert_pem_val;
    if (!gg_map_get(
            gg_obj_into_map(result), GG_STR("certificatePem"), &cert_pem_val
        )) {
        GG_LOGE("Failed to get certificatePem from response.");
        return GG_ERR_INVALID;
    }

    if (gg_obj_type(*cert_pem_val) != GG_TYPE_BUF) {
        GG_LOGE("Invalid certificatePem type in response.");
        return GG_ERR_INVALID;
    }

    GgBuffer cert_pem = gg_obj_into_buf(*cert_pem_val);
    ssize_t written = write(certificate_fd, cert_pem.data, cert_pem.len);
    if (written != (ssize_t) cert_pem.len) {
        GG_LOGE("Failed to write certificate to file.");
        return GG_ERR_FAILURE;
    }

    GG_LOGD("Certificate ownership token received (length: %zu)", token.len);
    return GG_ERR_OK;
}

static GgError register_thing_name_request(
    GgBuffer template_name,
    GgMap template_params,
    GgBuffer token,
    GgBuffer iotcored,
    GgBuffer *thing_name_out
) {
    uint8_t arena_mem[MAX_REGISTER_THING_PAYLOAD_SIZE];
    GgArena arena = gg_arena_init(GG_BUF(arena_mem));

    GgObject thing_payload_obj = gg_obj_map(GG_MAP(
        gg_kv(GG_STR("certificateOwnershipToken"), gg_obj_buf(token)),
        gg_kv(GG_STR("parameters"), gg_obj_map(template_params))
    ));

    uint8_t topic_mem[MAX_TOPIC_LEN];
    GgByteVec topic_vec = GG_BYTE_VEC(topic_mem);
    gg_byte_vec_chain_append(
        &(GgError) { GG_ERR_OK },
        &topic_vec,
        GG_STR("$aws/provisioning-templates/")
    );
    gg_byte_vec_chain_append(
        &(GgError) { GG_ERR_OK }, &topic_vec, template_name
    );
    gg_byte_vec_chain_append(
        &(GgError) { GG_ERR_OK }, &topic_vec, GG_STR("/provision/json")
    );

    GgObject result = { 0 };
    GgError ret = ggl_aws_iot_call(
        iotcored, topic_vec.buf, thing_payload_obj, true, &arena, &result
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GgObject *thing_name_val = { 0 };
    if (!gg_map_get(
            gg_obj_into_map(result), GG_STR("thingName"), &thing_name_val
        )) {
        uint8_t json_error_response[MAX_REQUEST_RESPONSE_SIZE] = { 0 };
        GgBuffer json_error_response_buf = GG_BUF(json_error_response);
        (void) gg_json_encode(result, gg_buf_writer(&json_error_response_buf));
        GG_LOGE(
            "Failed to get thing name from response. Response: (%.*s)",
            (int) json_error_response_buf.len,
            json_error_response_buf.data
        );

        return GG_ERR_INVALID;
    }

    if (gg_obj_type(*thing_name_val) != GG_TYPE_BUF) {
        GG_LOGE("Invalid thing name type in response.");
        return GG_ERR_INVALID;
    }

    GgBuffer thing_name = gg_obj_into_buf(*thing_name_val);

    ret = gg_buf_copy(thing_name, thing_name_out);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to copy thingName into the out buffer. Error: %d", ret);
        return ret;
    }

    GG_LOGI("Thing name received: %.*s", (int) thing_name.len, thing_name.data);
    return GG_ERR_OK;
}

GgError ggl_get_certificate_from_aws(
    GgBuffer csr_as_ggl_buffer,
    GgBuffer template_name,
    GgMap template_params,
    GgBuffer *thing_name_out,
    int certificate_fd
) {
    static uint8_t token_mem[MAX_TOKEN_SIZE];
    GgBuffer token = GG_BUF(token_mem);
    GgBuffer iotcored = GG_STR("iotcoredfleet");

    GgError ret
        = send_csr_request(csr_as_ggl_buffer, &token, iotcored, certificate_fd);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return register_thing_name_request(
        template_name, template_params, token, iotcored, thing_name_out
    );
}
