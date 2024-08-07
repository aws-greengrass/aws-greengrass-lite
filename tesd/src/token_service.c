// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "token_service.h"
#include <ggl/error.h>
#include <ggl/object.h>
#include <string.h>

GglError initiate_request(
    char* root_ca,
    char* cert_path,
    char* key_path,
    char* thing_name,
    char* role_alias,
    char* cert_endpoint
) {
    static char url_buf[2024] = {0};

    strncat(url_buf, "https://", strlen("https://"));
    strncat(url_buf, (char *) cert_endpoint, strlen(cert_endpoint));
    strncat(url_buf, "/role-aliases/", strlen("/role-aliases/"));
    strncat(url_buf, (char *) role_alias, strlen(role_alias));
    strncat(url_buf, "/credentials", strlen("/credentials"));

    //ggl_buffer = fetch_token(url, *request_body);

    return GGL_ERR_OK;
}