#include "curl_util.h"
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <time.h>
#include <stdio.h>

static const char HEADER_KEY[] = "x-amzn-iot-thingname";

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

static GglBuffer fetch_token(
    const char *url_for_token, RequestBody request_body
) {
    GglBuffer ggl_buffer;
    GGL_LOGI(
        "fetch_token",
        "Fetching token from credentials endpoint=%s, for iot thing=%s",
        url_for_token,
        request_body.thing_name
    );

    GglError error = init_curl(url_for_token);
    if (error == GGL_ERR_OK) {
        add_header(HEADER_KEY, request_body.thing_name);
        add_certificate_data(request_body);
        ggl_buffer = process_request();
    }

    return ggl_buffer;
}

static void generic_download(
    const char *url_for_generic_download, const char *file_path
) {
    GGL_LOGI(
        "generic_download",
        "downloading content from %s and storing to %s",
        url_for_generic_download,
        file_path
    );

    GglError error = init_curl(url_for_generic_download);
    if (error == GGL_ERR_OK) {
        GglBuffer ggl_buffer = process_request();
        write_buffer_to_file(file_path, &ggl_buffer);
    }
}

void http_with_tls(
    const char *url,
    const HttpMethod method,
    const RequestBody *request_body,
    const char *local_file_path,
    GglBuffer *ggl_buffer
) {
    if (request_body == NULL) {
        generic_download(url, local_file_path);
    } else {
        *ggl_buffer = fetch_token(url, *request_body);
    }
}

int main(int argc, char **argv) {
    // add the necessary logic here.
    return 0;
}
