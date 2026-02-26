#include "http_server.h"
#include <arpa/inet.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/util.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/json_encode.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <systemd/sd-daemon.h>
#include <stdbool.h>
#include <stdio.h>

struct evhttp_request;

static GgObject fetch_creds(GgArena *alloc) {
    GgBuffer tesd = GG_STR("aws_iot_tes");
    GgObject result = { 0 };
    GgMap params = { 0 };

    GgError error = ggl_call(
        tesd,
        GG_STR("request_credentials_formatted"),
        params,
        NULL,
        alloc,
        &result
    );

    if (error != GG_ERR_OK) {
        GG_LOGE("tes request failed....");
    } else {
        if (gg_obj_type(result) == GG_TYPE_BUF) {
            GgBuffer result_buf = gg_obj_into_buf(result);
            GG_LOGI(
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
    GG_LOGI("Attempting to vend creds for a request.");
    struct evkeyvalq *headers = evhttp_request_get_input_headers(req);

    // Check for the required header
    const char *auth_header = evhttp_find_header(headers, "Authorization");
    if (!auth_header) {
        GG_LOGE("Missing Authorization header.");
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
        GG_LOGE("svcuid character count must be exactly 16.");
        // Respond with 400 Bad Request
        struct evbuffer *response = evbuffer_new();
        if (response) {
            evbuffer_add_printf(response, "SVCUID length must be exactly 16.");
            evhttp_send_reply(req, HTTP_BADREQUEST, "Bad Request", response);
            evbuffer_free(response);
        }
        return;
    }

    GgBuffer auth_header_buf
        = { .data = (uint8_t *) auth_header, .len = auth_header_len };

    GgMap svcuid_map
        = GG_MAP(gg_kv(GG_STR("svcuid"), gg_obj_buf(auth_header_buf)));

    GgObject result_obj;
    GgError res = ggl_call(
        GG_STR("ipc_component"),
        GG_STR("verify_svcuid"),
        svcuid_map,
        NULL,
        NULL,
        &result_obj
    );
    if (res != GG_ERR_OK) {
        GG_LOGE("Failed to make an IPC call to ipc_component to check svcuid.");
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

    if (gg_obj_type(result_obj) != GG_TYPE_BOOLEAN) {
        GG_LOGE("Call to verify_svcuid responded with non-bool value.");
        return;
    }

    bool result = gg_obj_into_bool(result_obj);
    if (!result) {
        GG_LOGE("svcuid cannot be found");
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

    static uint8_t alloc_mem[8192];
    GgArena alloc = gg_arena_init(GG_BUF(alloc_mem));
    GgObject tes_formatted_obj = fetch_creds(&alloc);

    static uint8_t response_cred_mem[8192];
    GgByteVec response_cred_buffer = GG_BYTE_VEC(response_cred_mem);

    GgError ret_err_json = gg_json_encode(
        tes_formatted_obj, gg_byte_vec_writer(&response_cred_buffer)
    );
    if (ret_err_json != GG_ERR_OK) {
        GG_LOGE("Failed to convert the json.");
        return;
    }

    struct evbuffer *buf = evbuffer_new();

    if (!buf) {
        GG_LOGI("Failed to create response buffer.");
        return;
    }

    GG_LOGD("Successfully vended credentials for a request.");

    // Add the response data to the evbuffer
    evbuffer_add(
        buf, response_cred_buffer.buf.data, response_cred_buffer.buf.len
    );

    evhttp_send_reply(req, HTTP_OK, "OK", buf);
    evbuffer_free(buf);
}

static void default_handler(struct evhttp_request *req, void *arg) {
    (void) arg;

    GgBuffer response_cred_buffer
        = GG_STR("Only /2016-11-01/credentialprovider/ uri is supported.");
    struct evbuffer *buf = evbuffer_new();

    if (!buf) {
        GG_LOGE("Failed to create response buffer.");
        return;
    }

    // Add the response data to the evbuffer
    evbuffer_add(buf, response_cred_buffer.data, response_cred_buffer.len);

    evhttp_send_reply(req, HTTP_BADREQUEST, "Bad Request", buf);
    evbuffer_free(buf);
}

GgError http_server(void) {
    struct event_base *base = NULL;
    struct evhttp *http;
    struct evhttp_bound_socket *handle;

    uint16_t port = 0; // Let the OS choose a random free port

    // Create an event_base, which is the core of libevent
    base = event_base_new();
    if (!base) {
        GG_LOGE("Could not initialize libevent.");
        return GG_ERR_FAILURE;
    }

    // Create a new HTTP server
    http = evhttp_new(base);
    if (!http) {
        GG_LOGE("Could not create evhttp. Exiting...");
        return GG_ERR_FAILURE;
    }

    // Set a callback for requests to "/2016-11-01/credentialprovider/"
    evhttp_set_cb(
        http, "/2016-11-01/credentialprovider/", request_handler, NULL
    );
    evhttp_set_gencb(http, default_handler, NULL);

    // Bind to available  port
    handle = evhttp_bind_socket_with_handle(http, "0.0.0.0", 0);
    if (!handle) {
        GG_LOGE("Could not bind to any port. Exiting...");
        return GG_ERR_FAILURE;
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
        GG_LOGI("Listening on port http://localhost:%d\n", port);
    } else {
        GG_LOGE("Could not fetch the to any port url. Exiting...");
    }

    uint8_t port_mem[8];
    GgBuffer port_as_buffer = GG_BUF(port_mem);
    int ret_convert = snprintf(
        (char *) port_as_buffer.data, port_as_buffer.len, "%" PRId16, port
    );
    if (ret_convert < 0) {
        GG_LOGE("Error parsing the port value as string.");
        return GG_ERR_FAILURE;
    }
    if ((size_t) ret_convert > port_as_buffer.len) {
        GG_LOGE("Insufficient buffer space to store port data.");
        return GG_ERR_NOMEM;
    }
    port_as_buffer.len = (size_t) ret_convert;
    GG_LOGD(
        "Values when read in memory port:%.*s, len: %d, ret:%d\n",
        (int) port_as_buffer.len,
        port_as_buffer.data,
        (int) port_as_buffer.len,
        ret_convert
    );

    GgError ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.TokenExchangeService"),
            GG_STR("version")
        ),
        gg_obj_buf(GG_STR(GGL_VERSION)),
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Error writing the TES version to the config.");
        return ret;
    }

    ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.TokenExchangeService"),
            GG_STR("configArn")
        ),
        gg_obj_list(GG_LIST()),
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to write configuration arn list for TES to the config."
        );
        return ret;
    }

    ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.TokenExchangeService"),
            GG_STR("configuration"),
            GG_STR("port")
        ),
        gg_obj_buf(port_as_buffer),
        NULL
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    int ret_val = sd_notify(0, "READY=1");
    if (ret_val < 0) {
        GG_LOGE("Unable to update component state (errno=%d)", -ret);
        return GG_ERR_FATAL;
    }

    // Start the event loop
    event_base_dispatch(base);

    // Cleanup
    evhttp_free(http);
    event_base_free(base);

    return GG_ERR_OK;
}
