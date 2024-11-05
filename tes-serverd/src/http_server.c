#include "http_server.h"
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

struct evhttp_request;

static GglObject fetch_creds(GglBumpAlloc the_allocator) {
    GglBuffer tesd = GGL_STR("aws_iot_tes");
    GglObject result;
    GglMap params = { 0 };

    GglError error = ggl_call(
        tesd,
        GGL_STR("request_credentials_formatted"),
        params,
        NULL,
        &the_allocator.alloc,
        &result
    );

    if (error != GGL_ERR_OK) {
        GGL_LOGE("tes request failed....");
    } else {
        if (result.type == GGL_TYPE_BUF) {
            GGL_LOGI(
                "read value: %.*s",
                (int) result.buf.len,
                (char *) result.buf.data
            );
        }
    }

    return result;
}

static void request_handler(struct evhttp_request *req, void *arg) {
    (void) arg;
    static uint8_t big_buffer_for_bump[4096];
    static uint8_t temp_payload_alloc2[4096];

    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));
    GglObject tes_formatted_obj = fetch_creds(the_allocator);
    GglBuffer response_cred_buffer = GGL_BUF(temp_payload_alloc2);

    GglError ret_err_json
        = ggl_json_encode(tes_formatted_obj, &response_cred_buffer);
    if (ret_err_json != GGL_ERR_OK) {
        GGL_LOGE("Failed to convert the json");
        return;
    }

    struct evbuffer *buf = evbuffer_new();

    if (!buf) {
        GGL_LOGE("Failed to create response buffer");
        return;
    }

    // Add the response data to the evbuffer
    evbuffer_add(buf, response_cred_buffer.data, response_cred_buffer.len);

    evhttp_send_reply(req, 200, "OK", buf);
    evbuffer_free(buf);
}

GglError http_server(void) {
    struct event_base *base;
    struct evhttp *http;
    struct evhttp_bound_socket *handle;
    static char url_address[64] = { 0 };
    static char port_as_string[8] = { 0 };
    static uint16_t port = 0000;

    static uint8_t user_address_mem[128] = { 0 };
    GglBuffer user_address = GGL_BUF(user_address_mem);
    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.Nucleus-Lite"),
            GGL_STR("configuration"),
            GGL_STR("tesCredUrl")
        ),
        &user_address
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (user_address.data[0] == 'h') {
        memcpy(url_address, user_address.data + 7, user_address.len - 6 - 7);
    } else {
        GGL_LOGE(
            "The parameter tesCredUrl url can only is prefixed with http:\\ or "
            "with just the ip address(example: 127.0.0.1:8080/), any other "
            "combination is not supported."
        );
        return 1;
    }
    memcpy(port_as_string, user_address.data + user_address.len - 5, 5);
    port = (uint16_t) atoi(port_as_string);

    // Create an event_base, which is the core of libevent
    base = event_base_new();
    if (!base) {
        GGL_LOGE("Could not initialize libevent");
        return 1;
    }

    // Create a new HTTP server
    http = evhttp_new(base);
    if (!http) {
        GGL_LOGE("Could not create evhttp. Exiting.");
        return 1;
    }

    // Set a callback for requests to "/"
    evhttp_set_gencb(http, request_handler, NULL);

    // Bind to port 8080
    handle = evhttp_bind_socket_with_handle(http, url_address, port);
    if (!handle) {
        GGL_LOGE(
            "Could not bind to port http://%.*s:%d Exiting.",
            (int) strlen(url_address),
            url_address,
            port
        );
        return 1;
    }

    GGL_LOGI(
        "Server started on http://%.*s:%d...",
        (int) strlen(url_address),
        url_address,
        port
    );

    // Start the event loop
    event_base_dispatch(base);

    // Cleanup
    evhttp_free(http);
    event_base_free(base);

    return 0;
}
