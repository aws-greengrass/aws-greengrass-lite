#ifndef GGL_HTTP_API_ECR_H
#define GGL_HTTP_API_ECR_H

#include <gg/error.h>
#include <gg/types.h>
#include <ggl/http.h>
#include <stdint.h>

GgError ggl_http_ecr_get_authorization_token(
    SigV4Details sigv4_details,
    uint16_t *http_response_code,
    GgBuffer *response_buffer
);

#endif
