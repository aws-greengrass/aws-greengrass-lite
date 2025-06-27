#ifndef GGL_HTTP_API_ECR_H
#define GGL_HTTP_API_ECR_H

#include "ggl/http.h"
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <stdint.h>

GglError ggl_http_ecr_get_authorization_token(
    SigV4Details sigv4_details,
    uint16_t *http_response_code,
    GglBuffer *response_buffer
);

#endif
