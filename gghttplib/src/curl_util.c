#include <string.h>
#include <stdlib.h>
#include <ggl/log.h>
#include "curl_util.h"

#define MAX_HEADER_LENGTH 1000

CURL *curl;
struct curl_slist *headers_list;

static size_t write_response_to_buffer(
    void *response_data, size_t size, size_t nmemb, GglBuffer *output_buffer
) {
    size_t size_of_response_data = size * nmemb;
    uint8_t *ptr = realloc(
        output_buffer->data, output_buffer->len + size_of_response_data + 1
    );

    output_buffer->data = ptr;
    memcpy(
        output_buffer->data + output_buffer->len,
        response_data,
        size_of_response_data
    );
    output_buffer->len += size_of_response_data;
    output_buffer->data[output_buffer->len] = 0;

    return size_of_response_data;
}

GglError init_curl(const char *url) {
    headers_list = NULL;
    curl = curl_easy_init();

    if (curl == NULL) {
        GGL_LOGE(
            "init_curl", "Cannot create instance of curl for the url=%s", url
        );
        return GGL_ERR_FAILURE;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    return GGL_ERR_OK;
}

static void destroy_curl(void) {
    headers_list = NULL;
    curl_easy_cleanup(curl);
}

void add_header(const char header_key[], const char *header_value) {
    char header[MAX_HEADER_LENGTH];
    sprintf(header, "%s: %s", header_key, header_value);
    headers_list = curl_slist_append(headers_list, header);
}

void add_certificate_data(RequestBody request_data) {
    curl_easy_setopt(curl, CURLOPT_SSLCERT, request_data.cert_path);
    curl_easy_setopt(curl, CURLOPT_SSLKEY, request_data.p_key_path);
    curl_easy_setopt(curl, CURLOPT_CAPATH, request_data.root_ca_path);
}

GglBuffer process_request(void) {
    GglBuffer response_buffer = { 0 };

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response_to_buffer);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);

    CURLcode http_response_code = curl_easy_perform(curl);

    if (http_response_code != CURLE_OK) {
        GGL_LOGE(
            "process_request",
            "curl_easy_perfom() failed: %s",
            curl_easy_strerror(http_response_code)
        );
    }

    destroy_curl();
    return response_buffer;
}
