#include "http_server.h"
#include "inttypes.h"
#include "netinet/in.h"
#include "stdbool.h"
#include "stdio.h"
#include <arpa/inet.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/util.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/version.h>
#include <string.h>
#include <sys/socket.h>
#include <systemd/sd-daemon.h>
#include <stdint.h>

struct evhttp_request;

static GglObject fetch_creds(GglArena *alloc) {
    GglBuffer tesd = GGL_STR("aws_iot_tes");
    GglObject result = { 0 };
    GglMap params = { 0 };

    GglError error = ggl_call(
        tesd,
        GGL_STR("request_credentials_formatted"),
        params,
        NULL,
        alloc,
        &result
    );

    if (error != GGL_ERR_OK) {
        GGL_LOGE("tes request failed....");
    } else {
        if (ggl_obj_type(result) == GGL_TYPE_BUF) {
            GglBuffer result_buf = ggl_obj_into_buf(result);
            GGL_LOGI(
                "read value: %.*s",
                (int) result_buf.len,
                (char *) result_buf.data
            );
        }
    }

    return result;
}

static void request_handler(struct evhttp_request *req, void *arg) {
    (void) arg;
    GGL_LOGI("Attempting to vend creds for a request.");
    struct evkeyvalq *headers = evhttp_request_get_input_headers(req);

    // Check for the required header
    const char *auth_header = evhttp_find_header(headers, "Authorization");
    if (!auth_header) {
        GGL_LOGE("Missing Authorization header.");
        // Respond with 400 Bad Request
        struct evbuffer *response = evbuffer_new();
        if (response) {
            evbuffer_add_printf(
                response,
                "Authorization header is needed to process the request."
            );
            evhttp_send_reply(req, HTTP_BADREQUEST, "Bad Request", response);
            evbuffer_free(response);
        }
        return;
    }

    size_t auth_header_len = strlen(auth_header);
    if (auth_header_len != 16U) {
        GGL_LOGE("svcuid character count must be exactly 16.");
        // Respond with 400 Bad Request
        struct evbuffer *response = evbuffer_new();
        if (response) {
            evbuffer_add_printf(response, "SVCUID length must be exactly 16.");
            evhttp_send_reply(req, HTTP_BADREQUEST, "Bad Request", response);
            evbuffer_free(response);
        }
        return;
    }

    GglBuffer auth_header_buf
        = { .data = (uint8_t *) auth_header, .len = auth_header_len };

    GglMap svcuid_map
        = GGL_MAP({ GGL_STR("svcuid"), ggl_obj_buf(auth_header_buf) }, );

    GglObject result_obj;
    GglError res = ggl_call(
        GGL_STR("ipc_component"),
        GGL_STR("verify_svcuid"),
        svcuid_map,
        NULL,
        NULL,
        &result_obj
    );
    if (res != GGL_ERR_OK) {
        GGL_LOGE("Failed to make an IPC call to ipc_component to check svcuid."
        );
        // Respond with 500 Server unavailable
        struct evbuffer *response = evbuffer_new();
        if (response) {
            evbuffer_add_printf(response, "Failed to fetch SVCUID. Try again.");
            evhttp_send_reply(
                req, HTTP_SERVUNAVAIL, "Server unavailable", response
            );
            evbuffer_free(response);
        }
        return;
    }

    if (ggl_obj_type(result_obj) != GGL_TYPE_BOOLEAN) {
        GGL_LOGE("Call to verify_svcuid responded with non-bool value.");
        return;
    }

    bool result = ggl_obj_into_bool(result_obj);
    if (!result) {
        GGL_LOGE("svcuid cannot be found");
        // Respond with 404 not found.
        struct evbuffer *response = evbuffer_new();
        if (response) {
            evbuffer_add_printf(response, "No such svcuid present.");
            evhttp_send_reply(
                req, HTTP_NOTFOUND, "Server unavailable", response
            );
            evbuffer_free(response);
        }
        return;
    }

    static uint8_t alloc_mem[4096];
    GglArena alloc = ggl_arena_init(GGL_BUF(alloc_mem));
    GglObject tes_formatted_obj = fetch_creds(&alloc);

    static uint8_t response_cred_mem[4096];
    GglBuffer response_cred_buffer = GGL_BUF(response_cred_mem);

    GglError ret_err_json
        = ggl_json_encode(tes_formatted_obj, &response_cred_buffer);
    if (ret_err_json != GGL_ERR_OK) {
        GGL_LOGE("Failed to convert the json.");
        return;
    }

    struct evbuffer *buf = evbuffer_new();

    if (!buf) {
        GGL_LOGI("Failed to create response buffer.");
        return;
    }

    GGL_LOGD("Successfully vended credentials for a request.");

    // Add the response data to the evbuffer
    evbuffer_add(buf, response_cred_buffer.data, response_cred_buffer.len);

    evhttp_send_reply(req, HTTP_OK, "OK", buf);
    evbuffer_free(buf);
}

static void default_handler(struct evhttp_request *req, void *arg) {
    (void) arg;

    GglBuffer response_cred_buffer
        = GGL_STR("Only /2016-11-01/credentialprovider/ uri is supported.");
    struct evbuffer *buf = evbuffer_new();

    if (!buf) {
        GGL_LOGE("Failed to create response buffer.");
        return;
    }

    // Add the response data to the evbuffer
    evbuffer_add(buf, response_cred_buffer.data, response_cred_buffer.len);

    evhttp_send_reply(req, HTTP_BADREQUEST, "Bad Request", buf);
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
        GGL_LOGE("Could not initialize libevent.");
        return GGL_ERR_FAILURE;
    }

    // Create a new HTTP server
    http = evhttp_new(base);
    if (!http) {
        GGL_LOGE("Could not create evhttp. Exiting...");
        return GGL_ERR_FAILURE;
    }

    // Set a callback for requests to "/2016-11-01/credentialprovider/"
    evhttp_set_cb(
        http, "/2016-11-01/credentialprovider/", request_handler, NULL
    );
    evhttp_set_gencb(http, default_handler, NULL);

    // Bind to available  port
    handle = evhttp_bind_socket_with_handle(http, "0.0.0.0", 0);
    if (!handle) {
        GGL_LOGE("Could not bind to any port. Exiting...");
        return GGL_ERR_FAILURE;
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
        GGL_LOGE("Could not fetch the to any port url. Exiting...");
    }

    uint8_t port_mem[8];
    GglBuffer port_as_buffer = GGL_BUF(port_mem);
    int ret_convert = snprintf(
        (char *) port_as_buffer.data, port_as_buffer.len, "%" PRId16, port
    );
    if (ret_convert < 0) {
        GGL_LOGE("Error parsing the port value as string.");
        return GGL_ERR_FAILURE;
    }
    if ((size_t) ret_convert > port_as_buffer.len) {
        GGL_LOGE("Insufficient buffer space to store port data.");
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
            GGL_STR("version")
        ),
        ggl_obj_buf(GGL_STR(GGL_VERSION)),
        NULL
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error writing the TES version to the config.");
        return ret;
    }

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.TokenExchangeService"),
            GGL_STR("configArn")
        ),
        ggl_obj_list(GGL_LIST()),
        NULL
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to write configuration arn list for TES to the config."
        );
        return ret;
    }

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.TokenExchangeService"),
            GGL_STR("configuration"),
            GGL_STR("port")
        ),
        ggl_obj_buf(port_as_buffer),
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

    return GGL_ERR_OK;
}
