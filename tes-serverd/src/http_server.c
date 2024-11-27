#include "http_server.h"
#include "inttypes.h"
#include "netinet/in.h"
#include "stdio.h"
#include <arpa/inet.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/util.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <sys/socket.h>
#include <systemd/sd-daemon.h>
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

    struct evkeyvalq *headers = evhttp_request_get_input_headers(req);

    // Check for the required header
    const char *auth_header = evhttp_find_header(headers, "Authorization");
    if (!auth_header) {
        GGL_LOGE("Missing Authorization header");
        // Respond with 400 Bad Request
        struct evbuffer *response = evbuffer_new();
        if (response) {
            evbuffer_add_printf(
                response,
                "Authorization header is needed to process the request"
            );
            evhttp_send_reply(req, HTTP_BADREQUEST, "Bad Request", response);
            evbuffer_free(response);
        }
        return;
    }
    // TODO: Check the Authorization's value

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

    evhttp_send_reply(req, HTTP_OK, "OK", buf);
    evbuffer_free(buf);
}

static void deafult_handler(struct evhttp_request *req, void *arg) {
    (void) arg;

    GglBuffer response_cred_buffer
        = GGL_STR("Only /2016-11-01/credentialprovider/ uri is supported");

    struct evbuffer *buf = evbuffer_new();

    if (!buf) {
        GGL_LOGE("Failed to create response buffer");
        return;
    }

    // Add the response data to the evbuffer
    evbuffer_add(buf, response_cred_buffer.data, response_cred_buffer.len);

    evhttp_send_reply(req, HTTP_NOCONTENT, "Forbidden", buf);
    evbuffer_free(buf);
}

GglError http_server(void) {
    struct event_base *base = NULL;
    struct evhttp *http;
    struct evhttp_bound_socket *handle;

    uint16_t port = 0; // Let the OS choose a random free port

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

    // Set a callback for requests to "/2016-11-01/credentialprovider/"
    evhttp_set_cb(
        http, "/2016-11-01/credentialprovider/", request_handler, NULL
    );
    evhttp_set_gencb(http, deafult_handler, NULL);

    // Bind to available  port
    handle = evhttp_bind_socket_with_handle(http, "0.0.0.0", 0);
    if (!handle) {
        GGL_LOGE("Could not bind to any port...Exiting.");
        return 1;
    }

    struct sockaddr_storage ss = { 0 };
    ev_socklen_t socklen = sizeof(ss);
    int fd = evhttp_bound_socket_get_fd(handle);

    if (getsockname(fd, (struct sockaddr *) &ss, &socklen) == 0) {
        if (ss.ss_family == AF_INET) {
            port = ntohs(((struct sockaddr_in *) &ss)->sin_port);
        } else if (ss.ss_family == AF_INET6) {
            port = ntohs(((struct sockaddr_in6 *) &ss)->sin6_port);
        }
        GGL_LOGI("Listening on port http://localhost:%d\n", port);
    } else {
        GGL_LOGE("Could not fetch the to any port url...Exiting.");
    }

    uint8_t port_mem[8];
    GglBuffer port_as_buffer = GGL_BUF(port_mem);
    int ret_convert = snprintf(
        (char *) port_as_buffer.data, port_as_buffer.len, "%" PRId16, port
    );
    if (ret_convert < 0) {
        GGL_LOGE("Error parsing the port value as string");
        return GGL_ERR_FAILURE;
    }
    if ((size_t) ret_convert > port_as_buffer.len) {
        GGL_LOGE("Insufficient buffer space to store port data");
        return GGL_ERR_NOMEM;
    }
    port_as_buffer.len = (size_t) ret_convert;
    GGL_LOGD(
        "Values when read in memory port:%.*s, len: %d, ret:%d\n",
        (int) port_as_buffer.len,
        port_as_buffer.data,
        (int) port_as_buffer.len,
        ret_convert
    );

    GglError ret = ggl_gg_config_write(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.TokenExchangeService"),
            GGL_STR("configuration"),
            GGL_STR("port")
        ),
        GGL_OBJ_BUF(port_as_buffer),
        NULL
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    int ret_val = sd_notify(0, "READY=1");
    if (ret_val < 0) {
        GGL_LOGE("Unable to update component state (errno=%d)", -ret);
        return GGL_ERR_FATAL;
    }

    // Start the event loop
    event_base_dispatch(base);

    // Cleanup
    evhttp_free(http);
    event_base_free(base);

    return 0;
}
