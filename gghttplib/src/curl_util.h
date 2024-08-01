#ifndef UTIL_H
#define UTIL_H

#include <curl/curl.h>
#include "ggl/object.h"
#include <ggl/error.h>

typedef struct RequestBody {
    char *thing_name;
    char *cert_path;
    char *p_key_path;
    char *root_ca_path;
} RequestBody;

typedef enum {
    GET,
    POST
} HttpMethod;

/*
This is a callback function which would be called by the libcurl library when
it recieves data from curl request.

This function take 4 arguments -
1) pointer to incoming data byte.
2) size of each element in byte, it is always 1.
3) total number of element bytes in data.
4) output buffer to store the response.
*/
static size_t write_response_to_buffer(
    void *response_data, size_t size, size_t nmemb, GglBuffer *output_buffer
);

/*
* Initlisize the curl object with the endpoint uri.
*/
GglError init_curl(const char *uri) ;

/*
* Add custom header to the curl request
*/
void add_header(const char header_key[],const char *header_value);

/*
* add custom certificate attribute to the request
*/
void add_certificate_data( RequestBody request_body);

/*
* Process the request.
*/
GglBuffer process_request(void);

#endif