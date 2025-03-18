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
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/cleanup.h>
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

static void cleanup_event_base_free(struct event_base **base) {
    if ((base != NULL) && (*base != NULL)) {
        event_base_free(*base);
    }
}

static void cleanup_evbuffer_free(struct evbuffer **buf) {
    if ((buf != NULL) && (*buf != NULL)) {
        evbuffer_free(*buf);
    }
}

static void cleanup_evhttp_free(struct evhttp **http) {
    if ((http != NULL) && (*http != NULL)) {
        evhttp_free(*http);
    }
}

static void cleanup_no_op(const void *data, size_t datlen, void *extra) {
    (void) data;
    (void) datlen;
    (void) extra;
}

static GglError create_response_buf(struct evbuffer **buf, GglBuffer message) {
    struct evbuffer *response = evbuffer_new();
    GGL_CLEANUP_ID(response_cleanup, cleanup_evbuffer_free, response);
    if (response == NULL) {
        GGL_LOGE("Failed to create event buffer.");
        return GGL_ERR_NOMEM;
    }
    int error = evbuffer_add_reference(
        response, message.data, message.len, cleanup_no_op, NULL
    );
    if (error == -1) {
        GGL_LOGE("Failed to add reference to buffer.");
        return GGL_ERR_FAILURE;
    }
    *buf = response;
    response_cleanup = NULL;
    return GGL_ERR_OK;
}

static GglError send_reply(
    struct evhttp_request *req, int code, char *reason, GglBuffer message
) {
    struct evbuffer *response = NULL;
    GglError ret = create_response_buf(&response, message);
    GGL_CLEANUP(cleanup_evbuffer_free, response);
    if (ret == GGL_ERR_OK) {
        evhttp_send_reply(req, code, reason, response);
    }
    return ret;
}

static void request_handler(struct evhttp_request *req, void *arg) {
    (void) arg;
    GGL_LOGI("TES request received.");
    struct evhttp_connection *evcon = evhttp_request_get_connection(req);
    if (evcon != NULL) {
        char *address = NULL;
        uint16_t port = 0;
        evhttp_connection_get_peer(evcon, &address, &port);
        if (address != NULL) {
            GglBuffer addr_buf = ggl_buffer_from_null_term(address);
            GGL_LOGD(
                "Request received from %.*s:%" PRIu16,
                (int) addr_buf.len,
                address,
                port
            );
        }
    }
    struct evkeyvalq *headers = evhttp_request_get_input_headers(req);

    // Check for the required header
    const char *auth_header = evhttp_find_header(headers, "Authorization");
    if (auth_header == NULL) {
        GGL_LOGE("Missing Authorization Header.");
        send_reply(
            req,
            401 /* Unauthorized */,
            "Unauthorized",
            GGL_STR("Missing Authorization Header.")
        );
        return;
    }

    GglBuffer auth_header_buf = ggl_buffer_from_null_term((char *) auth_header);
    if (auth_header_buf.len != 16U) {
        GGL_LOGE("SVCUID character count must be exactly 16.");
        send_reply(
            req,
            HTTP_BADREQUEST,
            "Bad Request",
            GGL_STR("SVCUID character count must be exactly 16.")
        );
        return;
    }

    GglMap svcuid_map
        = GGL_MAP({ GGL_STR("svcuid"), GGL_OBJ_BUF(auth_header_buf) }, );

    GglObject result = GGL_OBJ_BOOL(false);
    GglError res = ggl_call(
        GGL_STR("ipc_component"),
        GGL_STR("verify_svcuid"),
        svcuid_map,
        NULL,
        NULL,
        &result
    );
    if (res != GGL_ERR_OK) {
        GGL_LOGE("Bus call for SVCUID lookup failed.");
        send_reply(
            req,
            HTTP_SERVUNAVAIL,
            "Server Unavailable",
            GGL_STR("SVCUID lookup failed. Try again later.")
        );
        return;
    }

    if (!result.boolean) {
        GGL_LOGE("SVCUID could not be verified.");
        send_reply(
            req,
            403 /* Forbidden */,
            "Forbidden",
            GGL_STR("SVCUID could not be verified.")
        );
        return;
    }

    static uint8_t tes_cred_buffer[4096];
    static uint8_t json_encode_buffer[4096];

    GglBumpAlloc alloc = ggl_bump_alloc_init(GGL_BUF(tes_cred_buffer));
    GglObject tes_formatted_obj;
    GglError ret = ggl_call(
        GGL_STR("aws_iot_tes"),
        GGL_STR("request_credentials_formatted"),
        GGL_MAP(),
        NULL,
        &alloc.alloc,
        &tes_formatted_obj
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to make bus call to get TES.");
        send_reply(
            req,
            HTTP_SERVUNAVAIL,
            "Server Unavailable",
            GGL_STR("TES credential retrieval failed. Try again later.")
        );
        return;
    }

    GglBuffer response_cred_buffer = GGL_BUF(json_encode_buffer);
    GglError ret_err_json
        = ggl_json_encode(tes_formatted_obj, &response_cred_buffer);
    if (ret_err_json != GGL_ERR_OK) {
        GGL_LOGE("Failed to convert the json.");
        send_reply(
            req,
            HTTP_INTERNAL,
            "Internal Server Error",
            GGL_STR("Failed to retrieve TES credentials.")
        );
        return;
    }

    ret = send_reply(req, HTTP_OK, "OK", response_cred_buffer);
    if (ret == GGL_ERR_OK) {
        GGL_LOGD("Successfully vended credentials for a request.");
    }
}

static void default_handler(struct evhttp_request *req, void *arg) {
    (void) arg;
    send_reply(
        req,
        HTTP_NOTFOUND,
        "Not Found",
        GGL_STR("Only /2016-11-01/credentialprovider/ path is supported.")
    );
}

GglError http_server(void) {
    struct event_base *base = NULL;
    struct evhttp *http;
    struct evhttp_bound_socket *handle;

    uint16_t port = 0; // Let the OS choose a random free port

    // Create an event_base, which is the core of libevent
    base = event_base_new();
    GGL_CLEANUP(cleanup_event_base_free, base);
    if (!base) {
        GGL_LOGE("Could not initialize libevent. Exiting...");
        return GGL_ERR_FATAL;
    }

    // Create a new HTTP server
    http = evhttp_new(base);
    GGL_CLEANUP(cleanup_evhttp_free, http);
    if (!http) {
        GGL_LOGE("Could not create evhttp. Exiting...");
        return GGL_ERR_FATAL;
    }

    // Set a callback for requests to "/2016-11-01/credentialprovider/"
    int cb_ret = evhttp_set_cb(
        http, "/2016-11-01/credentialprovider/", request_handler, NULL
    );
    if (cb_ret != 0) {
        GGL_LOGE("Callback already exists or couldn't be created. Exiting...");
        return GGL_ERR_FATAL;
    }
    evhttp_set_gencb(http, default_handler, NULL);

    // Bind to available  port
    handle = evhttp_bind_socket_with_handle(http, "0.0.0.0", 0);
    if (!handle) {
        GGL_LOGE("Could not bind to any port. Exiting...");
        return GGL_ERR_FATAL;
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
        GGL_LOGE("Could not retrieve listen port. Exiting...");
        return GGL_ERR_FATAL;
    }

    uint8_t port_mem[8];
    GglBuffer port_as_buffer = GGL_BUF(port_mem);
    int ret_convert = snprintf(
        (char *) port_as_buffer.data, port_as_buffer.len, "%" PRId16, port
    );
    if (ret_convert < 0) {
        GGL_LOGE("Error parsing the port value as string. Exiting...");
        return GGL_ERR_FAILURE;
    }
    if ((size_t) ret_convert > port_as_buffer.len) {
        GGL_LOGE("Insufficient buffer space to store port data. Exiting...");
        return GGL_ERR_NOMEM;
    }
    port_as_buffer.len = (size_t) ret_convert;
    GGL_LOGD(
        "Read port: \"%.*s\"", (int) port_as_buffer.len, port_as_buffer.data
    );

    GglError ret = ggl_gg_config_write(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.TokenExchangeService"),
            GGL_STR("version")
        ),
        GGL_OBJ_BUF(GGL_STR(GGL_VERSION)),
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
        GGL_OBJ_LIST(GGL_LIST()),
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
        GGL_OBJ_BUF(port_as_buffer),
        NULL
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    int ret_val = sd_notify(0, "READY=1");
    if (ret_val < 0) {
        GGL_LOGE("Unable to update component state (errno=%d).", -ret);
    }

    // Start the event loop
    int err = event_base_dispatch(base);
    if (err != 0) {
        GGL_LOGE("Error'd out of event loop.");
        return GGL_ERR_FATAL;
    }

    GGL_LOGI("Shutting down TES server...");
    return GGL_ERR_OK;
}
