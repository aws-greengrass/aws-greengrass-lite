// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "gghttp_utll.h"
#include <ggl/log.h>
#include <stdio.h>
#include <stdlib.h>

static const char HEADER_KEY[] = "x-amzn-iot-thingname";

/**
 * @brief Writes the contents of a GglBuffer to a file.
 *
 * This function takes a file path and a GglBuffer as input, and writes the
 * contents of the buffer to the specified file. If the file does not exist,
 * it will be created. If the file already exists, it will be overwritten.
 *
 * @param file_path The path of the file to write the buffer contents to.
 * @param ggl_buffer A pointer to the GglBuffer containing the data to be
 * written.
 *
 * @return GGL_ERR_OK if the operation was successful, or GGL_ERR_FAILURE if an
 *         error occurred.
 */
static GglError write_buffer_to_file(
    const char *file_path, const GglBuffer *ggl_buffer
) {
    if (file_path == NULL || ggl_buffer->data == NULL || ggl_buffer->len == 0) {
        GGL_LOGE(
            "write_buffer_to_file",
            "Invalid file (%s) or NULL buffer content",
            file_path
        );
        return GGL_ERR_FAILURE;
    }

    // check if file exist, or create new
    FILE *file = fopen(file_path, "w");
    if (file == NULL) {
        fprintf(stderr, "Error: could not open file: %s\n", file_path);
        return GGL_ERR_FAILURE;
    }

    // Write data to the file
    size_t bytes_written
        = fwrite(ggl_buffer->data, sizeof(char), ggl_buffer->len, file);
    if (bytes_written != ggl_buffer->len) {
        GGL_LOGE(
            "write_buffer_to_file",
            "complete data not copied to the file=%s",
            file_path
        );
        fclose(file);
        return GGL_ERR_FAILURE;
    }

    // Close the file
    if (fclose(file) != 0) {
        GGL_LOGE("write_buffer_to_file", "Could not close file=%s", file_path);
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}

GglBuffer fetch_token(
    const char *url_for_token,
    const char *thing_name,
    CertificateDetails certificate_details
) {
    GglBuffer ggl_buffer = { 0 };

    GGL_LOGI(
        "fetch_token",
        "Fetching token from credentials endpoint=%s, for iot thing=%s",
        url_for_token,
        thing_name
    );

    CurlData curl_data = { 0 };
    GglError error = gghttplib_init_curl(&curl_data, url_for_token);
    if (error == GGL_ERR_OK) {
        gghttplib_add_header(&curl_data, HEADER_KEY, thing_name);
        gghttplib_add_certificate_data(&curl_data, certificate_details);
        ggl_buffer = gghttplib_process_request(&curl_data);
    }

    return ggl_buffer;
}

void generic_download(
    const char *url_for_generic_download, const char *file_path
) {
    GGL_LOGI(
        "generic_download",
        "downloading content from %s and storing to %s",
        url_for_generic_download,
        file_path
    );

    CurlData curl_data = { 0 };
    GglError error = gghttplib_init_curl(&curl_data, url_for_generic_download);
    if (error == GGL_ERR_OK) {
        GglBuffer ggl_buffer = gghttplib_process_request(&curl_data);
        write_buffer_to_file(file_path, &ggl_buffer);
        free(ggl_buffer.data);
    }
}
